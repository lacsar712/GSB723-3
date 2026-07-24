#include "routes.h"
#include "database.h"
#include "json_parser.h"
#include "logger.h"
#include "types.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static long long current_timestamp() {
  return (long long)time(NULL);
}

static void send_json_response(int client_socket, const char *json) {
  char header[512];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\nContent-Type: application/json; "
           "charset=UTF-8\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
  send(client_socket, header, strlen(header), 0);
  send(client_socket, json, strlen(json), 0);
}

static void send_json_status(int client_socket, int code, const char *msg) {
  char header[256];
  snprintf(header, sizeof(header),
           "HTTP/1.1 %d %s\r\nContent-Type: application/json; "
           "charset=UTF-8\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
           code, code == 200 ? "OK" : "Error");
  send(client_socket, header, strlen(header), 0);
  if (msg) {
    send(client_socket, msg, strlen(msg), 0);
  }
}

static void check_timeouts() {
  long long now = current_timestamp();
  for (int i = 0; i < order_count; i++) {
    Order *o = &orders[i];
    if (o->frozen) continue;

    if (strcmp(o->status, "delivered") == 0 && o->delivered_at > 0) {
      if (now - o->delivered_at > 2 * 3600) {
        strcpy(o->status, "completed");
        o->completed_at = now;
        log_message(LOG_INFO, "Auto-completed order %d (2h timeout)", o->id);
        save_data();
      }
    }
  }

  for (int i = 0; i < dispute_count; i++) {
    Dispute *d = &disputes[i];
    if (strcmp(d->status, "pending") != 0) continue;
    if (now - d->created_at > 24 * 3600) {
      Order *o = find_order(d->order_id);
      strcpy(d->status, "established");
      d->resolved_at = now;
      strcpy(d->resolution, "超时未补充说明，系统按接单方责任自动裁决成立");
      if (o) {
        o->frozen = 0;
        o->disputed = 0;
        strcpy(o->status, "refunded");
      }
      log_message(LOG_INFO, "Auto-resolved dispute %d as established", d->id);
      save_data();
    }
  }
}

static void send_file(int client_socket, const char *path,
                      const char *content_type) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    log_message(LOG_ERROR, "Failed to open file: %s", path);
    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    send(client_socket, response, strlen(response), 0);
    return;
  }

  char header[256];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", content_type);
  send(client_socket, header, strlen(header), 0);

  char file_buf[BUFFER_SIZE];
  int n;
  while ((n = read(fd, file_buf, BUFFER_SIZE)) > 0) {
    send(client_socket, file_buf, n, 0);
  }
  close(fd);
  log_message(LOG_INFO, "Served file: %s", path);
}

static void handle_register(int client_socket, char *body) {
  User u;
  memset(&u, 0, sizeof(User));
  u.credit_score = 100;

  parse_json_string(body, "username", u.username, sizeof(u.username));
  parse_json_string(body, "password", u.password, sizeof(u.password));
  parse_json_string(body, "realName", u.real_name, sizeof(u.real_name));
  parse_json_string(body, "major", u.major, sizeof(u.major));

  if (strlen(u.username) == 0 || strlen(u.password) == 0) {
    log_message(LOG_WARN, "Registration failed: missing credentials");
    char resp[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: "
                  "application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
                  "{\"status\":\"error\"}";
    send(client_socket, resp, strlen(resp), 0);
    return;
  }

  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, u.username) == 0) {
      log_message(LOG_WARN, "Registration failed: username already exists: %s",
                  u.username);
      char resp[] =
          "HTTP/1.1 409 Conflict\r\nContent-Type: "
          "application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
          "{\"status\":\"error\",\"message\":\"用户名已存在\"}";
      send(client_socket, resp, strlen(resp), 0);
      return;
    }
  }

  if (user_count < MAX_USERS) {
    users[user_count++] = u;
    save_data();
    log_message(LOG_INFO, "User registered: %s (%s)", u.username, u.real_name);

    char resp[512];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\n\r\n"
             "{\"status\":\"success\",\"username\":\"%s\",\"realName\":\"%s\","
             "\"major\":\"%s\",\"creditScore\":%d}",
             u.username, u.real_name, u.major, u.credit_score);
    send(client_socket, resp, strlen(resp), 0);
  } else {
    log_message(LOG_ERROR, "Registration failed: max users reached");
    char resp[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                  "application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
                  "{\"status\":\"error\"}";
    send(client_socket, resp, strlen(resp), 0);
  }
}

static void handle_login(int client_socket, char *body) {
  char user[50] = "", pass[50] = "";
  parse_json_string(body, "username", user, sizeof(user));
  parse_json_string(body, "password", pass, sizeof(pass));

  int found = -1;
  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, user) == 0 &&
        strcmp(users[i].password, pass) == 0) {
      found = i;
      break;
    }
  }

  if (found != -1) {
    char resp[512];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\n\r\n"
             "{\"status\":\"success\",\"username\":\"%s\",\"realName\":\"%s\","
             "\"major\":\"%s\",\"creditScore\":%d}",
             users[found].username, users[found].real_name,
             users[found].major, users[found].credit_score);
    send(client_socket, resp, strlen(resp), 0);
    log_message(LOG_INFO, "User logged in: %s", user);
  } else {
    char resp[] =
        "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n"
        "{\"status\":\"error\",\"message\":\"账号或密码错误\"}";
    send(client_socket, resp, strlen(resp), 0);
    log_message(LOG_WARN, "Login failed for user: %s", user);
  }
}

static void handle_get_orders(int client_socket, char *query_string) {
  check_timeouts();

  char creator[50] = "", worker[50] = "", category[50] = "";

  if (query_string) {
    char *cr_ptr = strstr(query_string, "creator=");
    if (cr_ptr) sscanf(cr_ptr + 8, "%[^& ]", creator);
    char *wr_ptr = strstr(query_string, "worker=");
    if (wr_ptr) sscanf(wr_ptr + 7, "%[^& ]", worker);
    char *ct_ptr = strstr(query_string, "category=");
    if (ct_ptr) sscanf(ct_ptr + 9, "%[^& ]", category);
  }

  char response_header[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json; "
                           "charset=UTF-8\r\nAccess-Control-Allow-Origin: "
                           "*\r\n\r\n";
  send(client_socket, response_header, strlen(response_header), 0);

  char *json = malloc(MAX_ORDERS * 2500);
  if (!json) {
    log_message(LOG_ERROR, "Failed to allocate memory for orders JSON");
    return;
  }
  memset(json, 0, MAX_ORDERS * 2500);
  get_orders_json(json, creator, worker, category);
  send(client_socket, json, strlen(json), 0);
  free(json);

  log_message(LOG_INFO, "Orders fetched - creator:%s worker:%s category:%s",
              creator[0] ? creator : "all", worker[0] ? worker : "all",
              category[0] ? category : "all");
}

static void handle_create_order(int client_socket, char *body) {
  Order new_order;
  memset(&new_order, 0, sizeof(Order));
  new_order.id = next_id++;
  new_order.created_at = current_timestamp();

  parse_json_string(body, "package", new_order.package_info,
                    sizeof(new_order.package_info));
  parse_json_string(body, "pickup", new_order.pickup_addr,
                    sizeof(new_order.pickup_addr));
  parse_json_string(body, "delivery", new_order.delivery_addr,
                    sizeof(new_order.delivery_addr));
  parse_json_string(body, "reward", new_order.reward, sizeof(new_order.reward));
  parse_json_string(body, "creator", new_order.creator,
                    sizeof(new_order.creator));

  if (strstr(new_order.pickup_addr, "菜鸟"))
    strcpy(new_order.category, "菜鸟");
  else if (strstr(new_order.pickup_addr, "顺丰"))
    strcpy(new_order.category, "顺丰");
  else if (strstr(new_order.pickup_addr, "京东"))
    strcpy(new_order.category, "京东");
  else if (strstr(new_order.pickup_addr, "中通") ||
           strstr(new_order.pickup_addr, "圆通"))
    strcpy(new_order.category, "中通");
  else
    strcpy(new_order.category, "其他");

  strcpy(new_order.status, "pending");

  if (order_count < MAX_ORDERS) {
    orders[order_count++] = new_order;
    save_data();
    log_message(LOG_INFO, "Order created: ID=%d by %s", new_order.id,
                new_order.creator);
    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: "
                      "application/json\r\nAccess-Control-Allow-Origin: "
                      "*\r\n\r\n{\"status\":\"success\"}";
    send(client_socket, response, strlen(response), 0);
  } else {
    log_message(LOG_ERROR, "Failed to create order: max orders reached");
    char response[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                      "application/json\r\nAccess-Control-Allow-Origin: "
                      "*\r\n\r\n{\"status\":\"error\"}";
    send(client_socket, response, strlen(response), 0);
  }
}

static void handle_update_status(int client_socket, char *body) {
  check_timeouts();

  int id = -1;
  char new_status[20] = "", worker[50] = "";

  char *id_ptr = strstr(body, "\"id\":");
  if (id_ptr) sscanf(id_ptr + 5, "%d", &id);

  parse_json_string(body, "status", new_status, sizeof(new_status));
  parse_json_string(body, "worker", worker, sizeof(worker));

  Order *o = find_order(id);
  if (!o) {
    log_message(LOG_WARN, "Order not found: ID=%d", id);
    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: "
                      "application/json\r\nAccess-Control-Allow-Origin: "
                      "*\r\n\r\n{\"status\":\"error\"}";
    send(client_socket, response, strlen(response), 0);
    return;
  }

  if (o->frozen && strcmp(new_status, "refunded") != 0 &&
      strcmp(new_status, "completed") != 0) {
    char resp[] = "HTTP/1.1 409 Conflict\r\nContent-Type: "
                  "application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
                  "{\"status\":\"error\",\"message\":\"订单已冻结，存在纠纷中\"}";
    send(client_socket, resp, strlen(resp), 0);
    return;
  }

  if (strcmp(new_status, "accepted") == 0 && strlen(worker) > 0) {
    User *w = find_user(worker);
    if (w && w->credit_score < 60) {
      char resp[] = "HTTP/1.1 403 Forbidden\r\nContent-Type: "
                    "application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
                    "{\"status\":\"error\",\"message\":\"信用分低于60分，暂不可接单\"}";
      send(client_socket, resp, strlen(resp), 0);
      return;
    }
    strcpy(o->worker, worker);
    o->accepted_at = current_timestamp();
  }

  if (strcmp(new_status, "delivered") == 0) {
    o->delivered_at = current_timestamp();
  }
  if (strcmp(new_status, "completed") == 0) {
    o->completed_at = current_timestamp();
  }

  strcpy(o->status, new_status);
  save_data();
  log_message(LOG_INFO, "Order %d status updated to: %s", id, new_status);

  char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n\r\n{\"status\":\"success\"}";
  send(client_socket, resp, strlen(resp), 0);
}

static void handle_update_profile(int client_socket, char *body) {
  char username[50] = "", real_name[50] = "", major[50] = "", pwd[50] = "";

  parse_json_string(body, "username", username, sizeof(username));
  parse_json_string(body, "realName", real_name, sizeof(real_name));
  parse_json_string(body, "major", major, sizeof(major));
  parse_json_string(body, "password", pwd, sizeof(pwd));

  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0) {
      if (strlen(real_name) > 0) strcpy(users[i].real_name, real_name);
      if (strlen(major) > 0) strcpy(users[i].major, major);
      if (strlen(pwd) > 0) strcpy(users[i].password, pwd);
      save_data();
      log_message(LOG_INFO, "Profile updated for user: %s", username);
      char response[] = "HTTP/1.1 200 OK\r\nContent-Type: "
                        "application/json\r\nAccess-Control-Allow-Origin: "
                        "*\r\n\r\n{\"status\":\"success\"}";
      send(client_socket, response, strlen(response), 0);
      return;
    }
  }

  log_message(LOG_WARN, "User not found for profile update: %s", username);
  char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: "
                    "application/json\r\nAccess-Control-Allow-Origin: "
                    "*\r\n\r\n{\"status\":\"error\"}";
  send(client_socket, response, strlen(response), 0);
}

static void handle_get_credit(int client_socket, char *query_string) {
  char username[50] = "";
  if (query_string) {
    char *u_ptr = strstr(query_string, "username=");
    if (u_ptr) sscanf(u_ptr + 9, "%[^& ]", username);
  }

  User *u = find_user(username);
  if (!u) {
    char resp[] = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n\r\n"
                  "{\"status\":\"error\",\"message\":\"用户不存在\"}";
    send(client_socket, resp, strlen(resp), 0);
    return;
  }

  int level = compute_credit_level(u->credit_score);
  const char *label = credit_level_label(level);
  int can_accept = u->credit_score >= 60 ? 1 : 0;

  char resp[512];
  snprintf(resp, sizeof(resp),
           "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n\r\n"
           "{\"status\":\"success\",\"username\":\"%s\",\"creditScore\":%d,"
           "\"level\":%d,\"levelLabel\":\"%s\",\"canAccept\":%s}",
           u->username, u->credit_score, level, label,
           can_accept ? "true" : "false");
  send(client_socket, resp, strlen(resp), 0);
}

static void handle_submit_rating(int client_socket, char *body) {
  int order_id = parse_json_int(body, "orderId", -1);
  int score = parse_json_int(body, "score", 0);
  char rater[50] = "", ratee[50] = "", comment[100] = "";
  parse_json_string(body, "rater", rater, sizeof(rater));
  parse_json_string(body, "ratee", ratee, sizeof(ratee));
  parse_json_string(body, "comment", comment, sizeof(comment));

  if (order_id < 0 || score < 1 || score > 5 || strlen(rater) == 0 ||
      strlen(ratee) == 0) {
    send_json_status(client_socket, 400,
                     "{\"status\":\"error\",\"message\":\"参数错误\"}");
    return;
  }

  Order *o = find_order(order_id);
  if (!o) {
    send_json_status(client_socket, 404,
                     "{\"status\":\"error\",\"message\":\"订单不存在\"}");
    return;
  }

  if (strcmp(o->status, "completed") != 0 &&
      strcmp(o->status, "refunded") != 0) {
    send_json_status(client_socket, 400,
                     "{\"status\":\"error\",\"message\":\"订单未完成，暂不可评价\"}");
    return;
  }

  if (strcmp(rater, o->creator) == 0) {
    if (o->creator_rated) {
      send_json_status(client_socket, 409,
                       "{\"status\":\"error\",\"message\":\"您已评价过该订单\"}");
      return;
    }
    if (strcmp(ratee, o->worker) != 0) {
      send_json_status(client_socket, 400,
                       "{\"status\":\"error\",\"message\":\"评价对象错误\"}");
      return;
    }
  } else if (strcmp(rater, o->worker) == 0) {
    if (o->worker_rated) {
      send_json_status(client_socket, 409,
                       "{\"status\":\"error\",\"message\":\"您已评价过该订单\"}");
      return;
    }
    if (strcmp(ratee, o->creator) != 0) {
      send_json_status(client_socket, 400,
                       "{\"status\":\"error\",\"message\":\"评价对象错误\"}");
      return;
    }
  } else {
    send_json_status(client_socket, 403,
                     "{\"status\":\"error\",\"message\":\"无权评价该订单\"}");
    return;
  }

  if (rating_count >= MAX_RATINGS) {
    send_json_status(client_socket, 500,
                     "{\"status\":\"error\",\"message\":\"评价数量已满\"}");
    return;
  }

  Rating *r = &ratings[rating_count++];
  memset(r, 0, sizeof(Rating));
  r->id = next_rating_id++;
  r->order_id = order_id;
  strcpy(r->rater, rater);
  strcpy(r->ratee, ratee);
  r->score = score;
  strcpy(r->comment, comment);
  r->created_at = current_timestamp();

  if (strcmp(rater, o->creator) == 0) o->creator_rated = 1;
  if (strcmp(rater, o->worker) == 0) o->worker_rated = 1;

  User *ratee_u = find_user(ratee);
  if (ratee_u) {
    double new_score_d = ratee_u->credit_score * 0.8 + score * 20 * 0.2;
    int new_score = (int)round(new_score_d);
    if (new_score < 0) new_score = 0;
    if (new_score > 100) new_score = 100;
    ratee_u->credit_score = new_score;
    log_message(LOG_INFO, "Credit score updated for %s: %d -> %d (rating %d)",
                ratee, ratee_u->credit_score, new_score, score);
  }

  save_data();
  send_json_status(client_socket, 200, "{\"status\":\"success\"}");
}

static void handle_get_ratings(int client_socket, char *query_string) {
  char username[50] = "", direction[20] = "";
  int limit = 10;

  if (query_string) {
    char *u_ptr = strstr(query_string, "username=");
    if (u_ptr) sscanf(u_ptr + 9, "%[^& ]", username);
    char *d_ptr = strstr(query_string, "direction=");
    if (d_ptr) sscanf(d_ptr + 10, "%[^& ]", direction);
    char *l_ptr = strstr(query_string, "limit=");
    if (l_ptr) limit = atoi(l_ptr + 6);
  }

  char *json = malloc(MAX_RATINGS * 1200);
  if (!json) {
    send_json_status(client_socket, 500, "{\"status\":\"error\"}");
    return;
  }
  memset(json, 0, MAX_RATINGS * 1200);
  get_ratings_json(json, username, direction, limit);
  send_json_response(client_socket, json);
  free(json);
}

static void handle_create_dispute(int client_socket, char *body) {
  check_timeouts();

  int order_id = parse_json_int(body, "orderId", -1);
  char initiator[50] = "", reason[200] = "";
  parse_json_string(body, "initiator", initiator, sizeof(initiator));
  parse_json_string(body, "reason", reason, sizeof(reason));

  if (order_id < 0 || strlen(initiator) == 0 || strlen(reason) == 0) {
    send_json_status(client_socket, 400,
                     "{\"status\":\"error\",\"message\":\"参数错误\"}");
    return;
  }

  Order *o = find_order(order_id);
  if (!o) {
    send_json_status(client_socket, 404,
                     "{\"status\":\"error\",\"message\":\"订单不存在\"}");
    return;
  }

  if (strcmp(o->creator, initiator) != 0 && strcmp(o->worker, initiator) != 0) {
    send_json_status(client_socket, 403,
                     "{\"status\":\"error\",\"message\":\"无权发起纠纷\"}");
    return;
  }

  if (strcmp(o->status, "accepted") != 0 &&
      strcmp(o->status, "delivered") != 0) {
    send_json_status(client_socket, 400,
                     "{\"status\":\"error\",\"message\":\"当前订单状态不可发起纠纷\"}");
    return;
  }

  for (int i = 0; i < dispute_count; i++) {
    if (disputes[i].order_id == order_id &&
        strcmp(disputes[i].initiator, initiator) == 0) {
      send_json_status(client_socket, 409,
                       "{\"status\":\"error\",\"message\":\"您已就该订单发起过纠纷\"}");
      return;
    }
  }

  if (o->disputed) {
    send_json_status(client_socket, 409,
                     "{\"status\":\"error\",\"message\":\"该订单已存在纠纷\"}");
    return;
  }

  if (dispute_count >= MAX_DISPUTES) {
    send_json_status(client_socket, 500,
                     "{\"status\":\"error\",\"message\":\"纠纷数量已满\"}");
    return;
  }

  Dispute *d = &disputes[dispute_count++];
  memset(d, 0, sizeof(Dispute));
  d->id = next_dispute_id++;
  d->order_id = order_id;
  strcpy(d->initiator, initiator);
  strcpy(d->reason, reason);
  strcpy(d->status, "pending");
  d->created_at = current_timestamp();
  strcpy(d->prev_order_status, o->status);

  o->frozen = 1;
  o->disputed = 1;
  strcpy(o->prev_status, o->status);

  save_data();
  log_message(LOG_INFO, "Dispute created: ID=%d for order %d by %s", d->id,
              order_id, initiator);

  char resp[256];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"success\",\"disputeId\":%d}", d->id);
  send_json_status(client_socket, 200, resp);
}

static void handle_get_disputes(int client_socket, char *query_string) {
  check_timeouts();

  char username[50] = "";
  if (query_string) {
    char *u_ptr = strstr(query_string, "username=");
    if (u_ptr) sscanf(u_ptr + 9, "%[^& ]", username);
  }

  char *json = malloc(MAX_DISPUTES * 2500);
  if (!json) {
    send_json_status(client_socket, 500, "{\"status\":\"error\"}");
    return;
  }
  memset(json, 0, MAX_DISPUTES * 2500);
  get_disputes_json(json, username);
  send_json_response(client_socket, json);
  free(json);
}

static void handle_get_dispute_detail(int client_socket, char *query_string) {
  check_timeouts();

  int id = -1;
  if (query_string) {
    char *id_ptr = strstr(query_string, "id=");
    if (id_ptr) id = atoi(id_ptr + 3);
  }

  Dispute *found = NULL;
  for (int i = 0; i < dispute_count; i++) {
    if (disputes[i].id == id) {
      found = &disputes[i];
      break;
    }
  }

  if (!found) {
    send_json_status(client_socket, 404,
                     "{\"status\":\"error\",\"message\":\"纠纷不存在\"}");
    return;
  }

  char *json = malloc(3000);
  if (!json) {
    send_json_status(client_socket, 500, "{\"status\":\"error\"}");
    return;
  }
  memset(json, 0, 3000);
  get_dispute_json(json, found);

  Order *o = find_order(found->order_id);
  if (o) {
    int json_len = (int)strlen(json);
    json[json_len - 1] = ',';
    char extra[1024];
    User *creator_u = find_user(o->creator);
    User *worker_u = find_user(o->worker);
    int c_score = creator_u ? creator_u->credit_score : 100;
    int w_score = worker_u ? worker_u->credit_score : 100;
    snprintf(extra, sizeof(extra),
             "\"order\":{\"id\":%d,\"package\":\"%s\",\"pickup\":\"%s\","
             "\"delivery\":\"%s\",\"reward\":\"%s\",\"status\":\"%s\","
             "\"creator\":\"%s\",\"worker\":\"%s\","
             "\"creatorCredit\":%d,\"workerCredit\":%d}}",
             o->id, o->package_info, o->pickup_addr, o->delivery_addr,
             o->reward, o->status, o->creator, o->worker, c_score, w_score);
    strncat(json, extra, 3000 - strlen(json) - 1);
  }

  send_json_response(client_socket, json);
  free(json);
}

static void handle_check_disputes(int client_socket) {
  check_timeouts();
  save_data();
  send_json_status(client_socket, 200, "{\"status\":\"success\"}");
}

void handle_request(int client_socket) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);

  if (bytes_read <= 0) {
    close(client_socket);
    return;
  }

  if (strstr(buffer, "OPTIONS ")) {
    char cors[] =
        "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n\r\n";
    send(client_socket, cors, strlen(cors), 0);
    close(client_socket);
    return;
  }

  if (strstr(buffer, "POST ")) {
    char *cl_ptr = strstr(buffer, "Content-Length: ");
    if (cl_ptr) {
      int clen = atoi(cl_ptr + 16);
      char *b_start = strstr(buffer, "\r\n\r\n");
      if (b_start) {
        b_start += 4;
        long current_body_len = bytes_read - (b_start - buffer);
        while (current_body_len < clen && bytes_read < BUFFER_SIZE - 1) {
          int n = read(client_socket, buffer + bytes_read,
                       BUFFER_SIZE - 1 - bytes_read);
          if (n <= 0) break;
          bytes_read += n;
          current_body_len += n;
        }
      }
    }
  }

  if (strstr(buffer, "GET / ") || strstr(buffer, "GET /index.html")) {
    send_file(client_socket, "frontend/index.html", "text/html; charset=UTF-8");
  } else if (strstr(buffer, "GET /style.css")) {
    send_file(client_socket, "frontend/css/style.css", "text/css");
  } else if (strstr(buffer, "GET /script.js")) {
    send_file(client_socket, "frontend/js/script.js", "application/javascript");
  } else if (strstr(buffer, "GET /credit-api.js")) {
    send_file(client_socket, "frontend/js/credit-api.js",
              "application/javascript");
  } else if (strstr(buffer, "GET /credit-ui.js")) {
    send_file(client_socket, "frontend/js/credit-ui.js",
              "application/javascript");
  } else if (strstr(buffer, "POST /api/register")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_register(client_socket, body + 4);
  } else if (strstr(buffer, "POST /api/login")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_login(client_socket, body + 4);
  } else if (strstr(buffer, "GET /api/orders")) {
    char *path_start = strstr(buffer, "GET /api/orders");
    char *q = strstr(path_start, "?");
    handle_get_orders(client_socket, q);
  } else if (strstr(buffer, "POST /api/orders")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_create_order(client_socket, body + 4);
  } else if (strstr(buffer, "POST /api/update_status")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_update_status(client_socket, body + 4);
  } else if (strstr(buffer, "POST /api/update_profile")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_update_profile(client_socket, body + 4);
  } else if (strstr(buffer, "GET /api/credit")) {
    char *path_start = strstr(buffer, "GET /api/credit");
    char *q = strstr(path_start, "?");
    handle_get_credit(client_socket, q);
  } else if (strstr(buffer, "POST /api/ratings") &&
             !strstr(buffer, "/api/ratings?")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_submit_rating(client_socket, body + 4);
  } else if (strstr(buffer, "GET /api/ratings")) {
    char *path_start = strstr(buffer, "GET /api/ratings");
    char *q = strstr(path_start, "?");
    handle_get_ratings(client_socket, q);
  } else if (strstr(buffer, "POST /api/disputes") &&
             !strstr(buffer, "/api/disputes/") &&
             !strstr(buffer, "/api/disputes?")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) handle_create_dispute(client_socket, body + 4);
  } else if (strstr(buffer, "GET /api/disputes?") ||
             (strstr(buffer, "GET /api/disputes") &&
              !strstr(buffer, "GET /api/disputes?"))) {
    char *path_start = strstr(buffer, "GET /api/disputes");
    char *q = strstr(path_start, "?");
    if (q && strstr(q, "id=")) {
      handle_get_dispute_detail(client_socket, q);
    } else {
      handle_get_disputes(client_socket, q);
    }
  } else if (strstr(buffer, "POST /api/disputes/check")) {
    handle_check_disputes(client_socket);
  } else {
    log_message(LOG_WARN, "404 Not Found: %.50s", buffer);
    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    send(client_socket, response, strlen(response), 0);
  }

  close(client_socket);
}
