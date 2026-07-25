#include "routes.h"
#include "credit.h"
#include "database.h"
#include "json_parser.h"
#include "logger.h"
#include "types.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* 转义字符串写入 JSON（处理引号、反斜杠、控制符），避免评语/原因中的特殊字符破坏 JSON */
static void json_escape(const char *src, char *dst, int max_len) {
  int j = 0;
  for (int i = 0; src[i] && j < max_len - 2; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == '"' || c == '\\') {
      dst[j++] = '\\';
      dst[j++] = c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      if (j < max_len - 2) {
        dst[j++] = ' ';
      }
    } else {
      dst[j++] = c;
    }
  }
  dst[j] = '\0';
}

/* 评分人脱敏：保留首字符，其余用 * 代替（按字节，中文也可读） */
static void mask_name(const char *src, char *dst, int max_len) {
  int len = (int)strlen(src);
  if (len == 0) {
    strcpy(dst, "匿名");
    return;
  }
  int j = 0;
  /* 保留第一个 UTF-8 字符 */
  int first_char_bytes = 1;
  unsigned char c0 = (unsigned char)src[0];
  if (c0 >= 0xF0)
    first_char_bytes = 4;
  else if (c0 >= 0xE0)
    first_char_bytes = 3;
  else if (c0 >= 0xC0)
    first_char_bytes = 2;
  for (int i = 0; i < first_char_bytes && src[i] && j < max_len - 1; i++)
    dst[j++] = src[i];
  int stars = 2;
  for (int s = 0; s < stars && j < max_len - 2; s++)
    dst[j++] = '*';
  dst[j] = '\0';
}

static void send_json(int client_socket, const char *status_line,
                      const char *json_body) {
  char header[128];
  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\nContent-Type: application/json; "
           "charset=UTF-8\r\n\r\n",
           status_line);
  send(client_socket, header, strlen(header), 0);
  send(client_socket, json_body, strlen(json_body), 0);
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

  parse_json_string(body, "username", u.username, sizeof(u.username));
  parse_json_string(body, "password", u.password, sizeof(u.password));
  parse_json_string(body, "realName", u.real_name, sizeof(u.real_name));
  parse_json_string(body, "major", u.major, sizeof(u.major));

  if (strlen(u.username) == 0 || strlen(u.password) == 0) {
    log_message(LOG_WARN, "Registration failed: missing credentials");
    char resp[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: "
                  "application/json\r\n\r\n{\"status\":\"error\"}";
    send(client_socket, resp, strlen(resp), 0);
    return;
  }

  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, u.username) == 0) {
      log_message(LOG_WARN, "Registration failed: username already exists: %s",
                  u.username);
      char resp[] =
          "HTTP/1.1 409 Conflict\r\nContent-Type: "
          "application/"
          "json\r\n\r\n{\"status\":\"error\",\"message\":\"用户名已存在\"}";
      send(client_socket, resp, strlen(resp), 0);
      return;
    }
  }

  if (user_count < MAX_USERS) {
    u.credit = INITIAL_CREDIT;
    users[user_count++] = u;
    save_data();
    log_message(LOG_INFO, "User registered: %s (%s)", u.username, u.real_name);

    char resp[512];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
             "{\"status\":\"success\",\"username\":\"%s\",\"realName\":\"%s\","
             "\"major\":\"%s\",\"credit\":%d}",
             u.username, u.real_name, u.major, u.credit);
    send(client_socket, resp, strlen(resp), 0);
  } else {
    log_message(LOG_ERROR, "Registration failed: max users reached");
    char resp[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                  "application/json\r\n\r\n{\"status\":\"error\"}";
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
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
             "{\"status\":\"success\",\"username\":\"%s\",\"realName\":\"%s\","
             "\"major\":\"%s\",\"credit\":%d}",
             users[found].username, users[found].real_name,
             users[found].major, users[found].credit);
    send(client_socket, resp, strlen(resp), 0);
    log_message(LOG_INFO, "User logged in: %s", user);
  } else {
    char resp[] =
        "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n\r\n"
        "{\"status\":\"error\",\"message\":\"账号或密码错误\"}";
    send(client_socket, resp, strlen(resp), 0);
    log_message(LOG_WARN, "Login failed for user: %s", user);
  }
}

static void handle_get_orders(int client_socket, char *query_string) {
  char creator[50] = "", worker[50] = "", category[50] = "";

  if (query_string) {
    char *cr_ptr = strstr(query_string, "creator=");
    if (cr_ptr)
      sscanf(cr_ptr + 8, "%[^& ]", creator);
    char *wr_ptr = strstr(query_string, "worker=");
    if (wr_ptr)
      sscanf(wr_ptr + 7, "%[^& ]", worker);
    char *ct_ptr = strstr(query_string, "category=");
    if (ct_ptr)
      sscanf(ct_ptr + 9, "%[^& ]", category);
  }

  char response_header[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json; "
                           "charset=UTF-8\r\n\r\n";
  send(client_socket, response_header, strlen(response_header), 0);

  char *json = malloc(MAX_ORDERS * 1024);
  if (!json) {
    log_message(LOG_ERROR, "Failed to allocate memory for orders JSON");
    return;
  }
  memset(json, 0, MAX_ORDERS * 1024);
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
                      "application/json\r\n\r\n{\"status\":\"success\"}";
    send(client_socket, response, strlen(response), 0);
  } else {
    log_message(LOG_ERROR, "Failed to create order: max orders reached");
    char response[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                      "application/json\r\n\r\n{\"status\":\"error\"}";
    send(client_socket, response, strlen(response), 0);
  }
}

static void handle_update_status(int client_socket, char *body) {
  int id = -1;
  char new_status[20] = "", worker[50] = "";

  char *id_ptr = strstr(body, "\"id\":");
  if (id_ptr)
    sscanf(id_ptr + 5, "%d", &id);

  parse_json_string(body, "status", new_status, sizeof(new_status));
  parse_json_string(body, "worker", worker, sizeof(worker));

  for (int i = 0; i < order_count; i++) {
    if (orders[i].id == id) {
      /* 冻结订单（有纠纷进行中）不可再做普通状态推进 */
      if (orders[i].frozen) {
        log_message(LOG_WARN, "Order %d is frozen by dispute", id);
        send_json(client_socket, "409 Conflict",
                  "{\"status\":\"error\",\"message\":\"订单存在进行中的纠纷，"
                  "已冻结，无法操作\"}");
        return;
      }
      /* 接单信用门槛：信用分低于 60 不可再接单（发布仍允许） */
      if (strcmp(new_status, "accepted") == 0 && strlen(worker) > 0) {
        User *wu = find_user(worker);
        if (wu && wu->credit < MIN_ACCEPT_CREDIT) {
          log_message(LOG_WARN, "User %s credit too low to accept: %d", worker,
                      wu->credit);
          send_json(client_socket, "403 Forbidden",
                    "{\"status\":\"error\",\"message\":\"信用分低于 60，"
                    "暂不可接单\"}");
          return;
        }
        strcpy(orders[i].worker, worker);
      }
      strcpy(orders[i].status, new_status);
      /* 记录送达时间，用于 2 小时自动完成 */
      if (strcmp(new_status, "delivered") == 0)
        orders[i].delivered_at = (long)time(NULL);
      save_data();
      log_message(LOG_INFO, "Order %d status updated to: %s", id, new_status);
      char response[] = "HTTP/1.1 200 OK\r\nContent-Type: "
                        "application/json\r\n\r\n{\"status\":\"success\"}";
      send(client_socket, response, strlen(response), 0);
      return;
    }
  }

  log_message(LOG_WARN, "Order not found or invalid: ID=%d", id);
  char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: "
                    "application/json\r\n\r\n{\"status\":\"error\"}";
  send(client_socket, response, strlen(response), 0);
}

static void handle_update_profile(int client_socket, char *body) {
  char username[50] = "", real_name[50] = "", major[50] = "", pwd[50] = "";

  parse_json_string(body, "username", username, sizeof(username));
  parse_json_string(body, "realName", real_name, sizeof(real_name));
  parse_json_string(body, "major", major, sizeof(major));
  parse_json_string(body, "password", pwd, sizeof(pwd));

  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0) {
      if (strlen(real_name) > 0)
        strcpy(users[i].real_name, real_name);
      if (strlen(major) > 0)
        strcpy(users[i].major, major);
      if (strlen(pwd) > 0)
        strcpy(users[i].password, pwd);
      save_data();
      log_message(LOG_INFO, "Profile updated for user: %s", username);
      char response[] = "HTTP/1.1 200 OK\r\nContent-Type: "
                        "application/json\r\n\r\n{\"status\":\"success\"}";
      send(client_socket, response, strlen(response), 0);
      return;
    }
  }

  log_message(LOG_WARN, "User not found for profile update: %s", username);
  char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: "
                    "application/json\r\n\r\n{\"status\":\"error\"}";
  send(client_socket, response, strlen(response), 0);
}

/* ============ 信用分 + 纠纷仲裁模块 ============ */

/* 提交评价：订单完成后，发布方与接单方各自只能评价对方一次。
 * body: {orderId, rater, score, comment} */
static void handle_submit_rating(int client_socket, char *body) {
  int order_id = -1, score = 0;
  char rater[50] = "", comment[160] = "";

  char *oid = strstr(body, "\"orderId\":");
  if (oid)
    sscanf(oid + 10, "%d", &order_id);
  char *sc = strstr(body, "\"score\":");
  if (sc)
    sscanf(sc + 8, "%d", &score);
  parse_json_string(body, "rater", rater, sizeof(rater));
  parse_json_string(body, "comment", comment, sizeof(comment));

  if (score < 1 || score > 5) {
    send_json(client_socket, "400 Bad Request",
              "{\"status\":\"error\",\"message\":\"评分需为 1-5 星\"}");
    return;
  }

  Order *o = NULL;
  for (int i = 0; i < order_count; i++)
    if (orders[i].id == order_id)
      o = &orders[i];

  if (!o) {
    send_json(client_socket, "404 Not Found",
              "{\"status\":\"error\",\"message\":\"订单不存在\"}");
    return;
  }
  if (strcmp(o->status, "completed") != 0) {
    send_json(client_socket, "409 Conflict",
              "{\"status\":\"error\",\"message\":\"订单未完成，暂不可评价\"}");
    return;
  }

  int is_creator = strcmp(o->creator, rater) == 0;
  int is_worker = strcmp(o->worker, rater) == 0;
  if (!is_creator && !is_worker) {
    send_json(client_socket, "403 Forbidden",
              "{\"status\":\"error\",\"message\":\"非订单参与方，无法评价\"}");
    return;
  }
  if ((is_creator && o->creator_rated) || (is_worker && o->worker_rated)) {
    send_json(client_socket, "409 Conflict",
              "{\"status\":\"error\",\"message\":\"你已评价过该订单\"}");
    return;
  }

  char ratee[50] = "";
  strcpy(ratee, is_creator ? o->worker : o->creator);
  if (strlen(ratee) == 0) {
    send_json(client_socket, "409 Conflict",
              "{\"status\":\"error\",\"message\":\"缺少被评价对象\"}");
    return;
  }

  if (rating_count >= MAX_RATINGS) {
    send_json(client_socket, "500 Internal Server Error",
              "{\"status\":\"error\"}");
    return;
  }

  /* 记录评分 */
  Rating *r = &ratings[rating_count++];
  memset(r, 0, sizeof(Rating));
  r->id = next_rating_id++;
  r->order_id = order_id;
  strcpy(r->rater, rater);
  strcpy(r->ratee, ratee);
  r->score = score;
  strncpy(r->comment, comment, sizeof(r->comment) - 1);
  r->created_at = (long)time(NULL);

  /* 更新被评价人信用分：新分 = round(旧分*0.8 + 评分*20*0.2) */
  User *ru = find_user(ratee);
  int new_credit = 0;
  int old_credit = 0;
  if (ru) {
    old_credit = ru->credit;
    ru->credit = credit_new_score(ru->credit, score);
    new_credit = ru->credit;
  }

  if (is_creator)
    o->creator_rated = 1;
  else
    o->worker_rated = 1;

  /* 写入信用事件时间线：给出评价 / 收到评价 / 信用分变更 */
  char given_detail[160], recv_detail[160];
  snprintf(given_detail, sizeof(given_detail), "你评价了「%s」%d 星%s%s", ratee,
           score, strlen(comment) ? "：" : "", comment);
  snprintf(recv_detail, sizeof(recv_detail), "收到一条 %d 星评价%s%s", score,
           strlen(comment) ? "：" : "", comment);
  add_credit_event(rater, "rating_given", "order", order_id, score, -1, -1,
                   given_detail);
  add_credit_event(ratee, "rating_received", "order", order_id, score, -1, -1,
                   recv_detail);
  if (ru) {
    char chg_detail[160];
    snprintf(chg_detail, sizeof(chg_detail),
             "因收到 %d 星评价，信用分 %d → %d", score, old_credit, new_credit);
    add_credit_event(ratee, "credit_change", "order", order_id, score,
                     old_credit, new_credit, chg_detail);
  }

  save_data();
  log_message(LOG_INFO, "Rating submitted: order=%d %s->%s score=%d credit=%d",
              order_id, rater, ratee, score, new_credit);

  char resp[128];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"success\",\"rateeCredit\":%d}", new_credit);
  send_json(client_socket, "200 OK", resp);
}

/* 信用档案：当前信用分、等级、近 10 条评分记录。
 * query: ?username=xxx&direction=received|given */
static void handle_get_credit(int client_socket, char *query_string) {
  char username[50] = "", direction[20] = "received";
  if (query_string) {
    char *u = strstr(query_string, "username=");
    if (u)
      sscanf(u + 9, "%[^& ]", username);
    char *d = strstr(query_string, "direction=");
    if (d)
      sscanf(d + 10, "%[^& ]", direction);
  }

  User *user = find_user(username);
  int credit = user ? user->credit : INITIAL_CREDIT;
  int by_received = strcmp(direction, "given") != 0;

  /* 是否有进行中纠纷：该用户作为发布方/接单方、且纠纷状态为 pending */
  int has_active_dispute = 0;
  for (int i = 0; i < dispute_count && !has_active_dispute; i++) {
    if (strcmp(disputes[i].status, "pending") != 0)
      continue;
    for (int k = 0; k < order_count; k++) {
      if (orders[k].id == disputes[i].order_id &&
          (strcmp(orders[k].creator, username) == 0 ||
           strcmp(orders[k].worker, username) == 0)) {
        has_active_dispute = 1;
        break;
      }
    }
  }

  char *json = malloc(64 * 1024);
  if (!json) {
    send_json(client_socket, "500 Internal Server Error",
              "{\"status\":\"error\"}");
    return;
  }
  int off = 0;
  off += sprintf(json + off,
                 "{\"status\":\"success\",\"username\":\"%s\",\"credit\":%d,"
                 "\"level\":\"%s\",\"levelKey\":\"%s\",\"canAccept\":%s,"
                 "\"hasActiveDispute\":%s,\"records\":[",
                 username, credit, credit_level_label(credit),
                 credit_level_key(credit),
                 credit < MIN_ACCEPT_CREDIT ? "false" : "true",
                 has_active_dispute ? "true" : "false");

  /* 倒序取最近 10 条匹配记录 */
  int emitted = 0;
  for (int i = rating_count - 1; i >= 0 && emitted < 10; i--) {
    Rating *r = &ratings[i];
    int match = by_received ? (strcmp(r->ratee, username) == 0)
                            : (strcmp(r->rater, username) == 0);
    if (!match)
      continue;

    char masked[64], esc_comment[320];
    /* 「我收到的」脱敏评分人；「我给出的」脱敏被评价人 */
    mask_name(by_received ? r->rater : r->ratee, masked, sizeof(masked));
    json_escape(r->comment, esc_comment, sizeof(esc_comment));

    off += sprintf(json + off,
                   "%s{\"id\":%d,\"orderId\":%d,\"counterpart\":\"%s\","
                   "\"score\":%d,\"comment\":\"%s\",\"time\":%ld}",
                   emitted ? "," : "", r->id, r->order_id, masked, r->score,
                   esc_comment, r->created_at);
    emitted++;
  }
  off += sprintf(json + off, "]}");

  send_json(client_socket, "200 OK", json);
  free(json);
  log_message(LOG_INFO, "Credit profile fetched for %s (%s)", username,
              direction);
}

/* 信用事件时间线：某用户相关事件按时间倒序返回。
 * query: ?username=xxx（可选 &limit=N，默认 50） */
static void handle_get_credit_events(int client_socket, char *query_string) {
  char username[50] = "";
  int limit = 50;
  if (query_string) {
    char *u = strstr(query_string, "username=");
    if (u)
      sscanf(u + 9, "%[^& ]", username);
    char *l = strstr(query_string, "limit=");
    if (l)
      sscanf(l + 6, "%d", &limit);
  }
  if (limit <= 0 || limit > MAX_EVENTS)
    limit = 50;

  char *json = malloc(MAX_EVENTS / 4 * 512 + 64);
  if (!json) {
    send_json(client_socket, "500 Internal Server Error",
              "{\"status\":\"error\"}");
    return;
  }
  int off = 0;
  json[off++] = '[';
  int emitted = 0;
  for (int i = event_count - 1; i >= 0 && emitted < limit; i--) {
    CreditEvent *e = &events[i];
    if (strcmp(e->owner, username) != 0)
      continue;
    char esc_detail[320];
    json_escape(e->detail, esc_detail, sizeof(esc_detail));
    off += sprintf(json + off,
                   "%s{\"id\":%d,\"type\":\"%s\",\"refType\":\"%s\","
                   "\"refId\":%d,\"score\":%d,\"oldCredit\":%d,"
                   "\"newCredit\":%d,\"detail\":\"%s\",\"time\":%ld}",
                   emitted ? "," : "", e->id, e->type, e->ref_type, e->ref_id,
                   e->score, e->old_credit, e->new_credit, esc_detail,
                   e->created_at);
    emitted++;
  }
  json[off++] = ']';
  json[off] = '\0';

  send_json(client_socket, "200 OK", json);
  free(json);
  log_message(LOG_INFO, "Credit events fetched for %s (%d)", username, emitted);
}

/* 发起纠纷：body {orderId, initiator, reason}
 * 允许状态：accepted / delivered；同一订单每方只能发起一次。 */
static void handle_create_dispute(int client_socket, char *body) {
  int order_id = -1;
  char initiator[50] = "", reason[320] = "";

  char *oid = strstr(body, "\"orderId\":");
  if (oid)
    sscanf(oid + 10, "%d", &order_id);
  parse_json_string(body, "initiator", initiator, sizeof(initiator));
  parse_json_string(body, "reason", reason, sizeof(reason));

  if (strlen(reason) == 0) {
    send_json(client_socket, "400 Bad Request",
              "{\"status\":\"error\",\"message\":\"请填写纠纷原因\"}");
    return;
  }

  Order *o = NULL;
  for (int i = 0; i < order_count; i++)
    if (orders[i].id == order_id)
      o = &orders[i];
  if (!o) {
    send_json(client_socket, "404 Not Found",
              "{\"status\":\"error\",\"message\":\"订单不存在\"}");
    return;
  }

  int is_party =
      strcmp(o->creator, initiator) == 0 || strcmp(o->worker, initiator) == 0;
  if (!is_party) {
    send_json(client_socket, "403 Forbidden",
              "{\"status\":\"error\",\"message\":\"非订单参与方\"}");
    return;
  }
  if (strcmp(o->status, "accepted") != 0 &&
      strcmp(o->status, "delivered") != 0) {
    send_json(client_socket, "409 Conflict",
              "{\"status\":\"error\",\"message\":\"当前订单状态不可发起纠纷\"}");
    return;
  }

  /* 同一订单每方只能发起一次纠纷 */
  for (int i = 0; i < dispute_count; i++) {
    if (disputes[i].order_id == order_id &&
        strcmp(disputes[i].initiator, initiator) == 0) {
      send_json(client_socket, "409 Conflict",
                "{\"status\":\"error\",\"message\":\"你已对该订单发起过纠纷\"}");
      return;
    }
  }

  if (dispute_count >= MAX_DISPUTES) {
    send_json(client_socket, "500 Internal Server Error",
              "{\"status\":\"error\"}");
    return;
  }

  Dispute *d = &disputes[dispute_count++];
  memset(d, 0, sizeof(Dispute));
  d->id = next_dispute_id++;
  d->order_id = order_id;
  strcpy(d->initiator, initiator);
  strncpy(d->reason, reason, sizeof(d->reason) - 1);
  strcpy(d->status, "pending");
  d->created_at = (long)time(NULL);
  d->resolved_at = 0;

  /* 发起后订单冻结，记录冻结前状态用于驳回恢复 */
  strcpy(o->prev_status, o->status);
  o->frozen = 1;

  /* 发起纠纷事件写入发起方时间线 */
  char dsp_detail[160];
  snprintf(dsp_detail, sizeof(dsp_detail), "你对订单 #%d 发起纠纷：%s", order_id,
           reason);
  add_credit_event(initiator, "dispute_created", "dispute", d->id, 0, -1, -1,
                   dsp_detail);

  save_data();
  log_message(LOG_INFO, "Dispute created: id=%d order=%d by %s", d->id,
              order_id, initiator);

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":\"success\",\"disputeId\":%d}",
           d->id);
  send_json(client_socket, "200 OK", resp);
}

/* 提交纠纷补充说明：body {disputeId, author, content, evidenceType?, evidenceDesc?}
 * 仅待裁决纠纷；发布方/接单方各自可提交一次；同一角色不可重复提交。 */
static void handle_submit_statement(int client_socket, char *body) {
  int dispute_id = -1;
  char author[50] = "", content[640] = "", evidence_type[16] = "",
       evidence_desc[160] = "";

  char *did = strstr(body, "\"disputeId\":");
  if (did)
    sscanf(did + 12, "%d", &dispute_id);
  parse_json_string(body, "author", author, sizeof(author));
  parse_json_string(body, "content", content, sizeof(content));
  parse_json_string(body, "evidenceType", evidence_type, sizeof(evidence_type));
  parse_json_string(body, "evidenceDesc", evidence_desc, sizeof(evidence_desc));

  if (strlen(content) == 0) {
    send_json(client_socket, "400 Bad Request",
              "{\"status\":\"error\",\"message\":\"请填写补充说明内容\"}");
    return;
  }

  Dispute *d = NULL;
  for (int i = 0; i < dispute_count; i++)
    if (disputes[i].id == dispute_id)
      d = &disputes[i];
  if (!d) {
    send_json(client_socket, "404 Not Found",
              "{\"status\":\"error\",\"message\":\"纠纷不存在\"}");
    return;
  }
  if (strcmp(d->status, "pending") != 0) {
    send_json(client_socket, "409 Conflict",
              "{\"status\":\"error\",\"message\":\"该纠纷已裁决，不可补充\"}");
    return;
  }

  Order *o = NULL;
  for (int i = 0; i < order_count; i++)
    if (orders[i].id == d->order_id)
      o = &orders[i];
  if (!o) {
    send_json(client_socket, "404 Not Found",
              "{\"status\":\"error\",\"message\":\"关联订单不存在\"}");
    return;
  }

  /* 判定角色：发布方 creator / 接单方 worker */
  char role[12] = "";
  if (strcmp(o->creator, author) == 0)
    strcpy(role, "creator");
  else if (strcmp(o->worker, author) == 0)
    strcpy(role, "worker");
  else {
    send_json(client_socket, "403 Forbidden",
              "{\"status\":\"error\",\"message\":\"非订单参与方\"}");
    return;
  }

  /* 同一角色不可重复提交 */
  for (int i = 0; i < statement_count; i++) {
    if (statements[i].dispute_id == dispute_id &&
        strcmp(statements[i].role, role) == 0) {
      send_json(client_socket, "409 Conflict",
                "{\"status\":\"error\",\"message\":\"你已提交过补充说明\"}");
      return;
    }
  }

  if (statement_count >= MAX_STATEMENTS) {
    send_json(client_socket, "500 Internal Server Error",
              "{\"status\":\"error\"}");
    return;
  }

  DisputeStatement *st = &statements[statement_count++];
  memset(st, 0, sizeof(DisputeStatement));
  st->id = next_statement_id++;
  st->dispute_id = dispute_id;
  strcpy(st->role, role);
  strcpy(st->author, author);
  strncpy(st->content, content, sizeof(st->content) - 1);
  strncpy(st->evidence_type, evidence_type, sizeof(st->evidence_type) - 1);
  strncpy(st->evidence_desc, evidence_desc, sizeof(st->evidence_desc) - 1);
  st->created_at = (long)time(NULL);

  save_data();
  log_message(LOG_INFO, "Statement submitted: dispute=%d role=%s by %s",
              dispute_id, role, author);
  send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
}

/* 纠纷列表：可选 ?username=xxx 只看与我相关的订单纠纷 */
static void handle_list_disputes(int client_socket, char *query_string) {
  char username[50] = "";
  if (query_string) {
    char *u = strstr(query_string, "username=");
    if (u)
      sscanf(u + 9, "%[^& ]", username);
  }

  char *json = malloc(MAX_DISPUTES * 3072);
  if (!json) {
    send_json(client_socket, "500 Internal Server Error",
              "{\"status\":\"error\"}");
    return;
  }
  int off = 0;
  json[off++] = '[';
  int first = 1;
  for (int i = dispute_count - 1; i >= 0; i--) {
    Dispute *d = &disputes[i];
    Order *o = NULL;
    for (int k = 0; k < order_count; k++)
      if (orders[k].id == d->order_id)
        o = &orders[k];

    if (strlen(username) > 0 && o) {
      if (strcmp(o->creator, username) != 0 &&
          strcmp(o->worker, username) != 0)
        continue;
    }

    char esc_reason[640], esc_verdict[640];
    json_escape(d->reason, esc_reason, sizeof(esc_reason));
    json_escape(d->verdict, esc_verdict, sizeof(esc_verdict));

    off += sprintf(
        json + off,
        "%s{\"id\":%d,\"orderId\":%d,\"initiator\":\"%s\",\"reason\":\"%s\","
        "\"status\":\"%s\",\"verdict\":\"%s\",\"createdAt\":%ld,"
        "\"resolvedAt\":%ld,\"package\":\"%s\",\"creator\":\"%s\","
        "\"worker\":\"%s\",\"reward\":\"%s\",\"statements\":[",
        first ? "" : ",", d->id, d->order_id, d->initiator, esc_reason,
        d->status, esc_verdict, d->created_at, d->resolved_at,
        o ? o->package_info : "", o ? o->creator : "", o ? o->worker : "",
        o ? o->reward : "");

    /* 附带该纠纷的补充说明（按提交时间正序） */
    int sfirst = 1;
    for (int s = 0; s < statement_count; s++) {
      DisputeStatement *st = &statements[s];
      if (st->dispute_id != d->id)
        continue;
      char esc_content[1300], esc_edesc[400];
      json_escape(st->content, esc_content, sizeof(esc_content));
      json_escape(st->evidence_desc, esc_edesc, sizeof(esc_edesc));
      off += sprintf(
          json + off,
          "%s{\"id\":%d,\"role\":\"%s\",\"author\":\"%s\",\"content\":\"%s\","
          "\"evidenceType\":\"%s\",\"evidenceDesc\":\"%s\",\"createdAt\":%ld}",
          sfirst ? "" : ",", st->id, st->role, st->author, esc_content,
          st->evidence_type, esc_edesc, st->created_at);
      sfirst = 0;
    }
    off += sprintf(json + off, "]}");
    first = 0;
  }
  json[off++] = ']';
  json[off] = '\0';

  send_json(client_socket, "200 OK", json);
  free(json);
  log_message(LOG_INFO, "Disputes listed for %s",
              username[0] ? username : "all");
}

/* 裁决纠纷：body {disputeId, verdict:"upheld"|"rejected"}
 * upheld=成立(退款,订单撤回退市)；rejected=驳回(恢复到发起前状态)。 */
static void handle_arbitrate_dispute(int client_socket, char *body) {
  int dispute_id = -1;
  char verdict[20] = "";
  char *did = strstr(body, "\"disputeId\":");
  if (did)
    sscanf(did + 12, "%d", &dispute_id);
  parse_json_string(body, "verdict", verdict, sizeof(verdict));

  Dispute *d = NULL;
  for (int i = 0; i < dispute_count; i++)
    if (disputes[i].id == dispute_id)
      d = &disputes[i];
  if (!d) {
    send_json(client_socket, "404 Not Found",
              "{\"status\":\"error\",\"message\":\"纠纷不存在\"}");
    return;
  }
  if (strcmp(d->status, "pending") != 0) {
    send_json(client_socket, "409 Conflict",
              "{\"status\":\"error\",\"message\":\"该纠纷已裁决\"}");
    return;
  }

  Order *o = NULL;
  for (int i = 0; i < order_count; i++)
    if (orders[i].id == d->order_id)
      o = &orders[i];

  if (strcmp(verdict, "upheld") == 0) {
    strcpy(d->status, "upheld");
    strcpy(d->verdict, "纠纷成立，悬赏金额已标记退回，订单撤回退市。");
    if (o) {
      o->frozen = 0;
      strcpy(o->status, "cancelled");
      add_credit_event(o->creator, "dispute_upheld", "dispute", d->id, 0, -1,
                       -1, "纠纷裁定成立，悬赏退回，订单已退市");
      if (strlen(o->worker) > 0)
        add_credit_event(o->worker, "dispute_upheld", "dispute", d->id, 0, -1,
                         -1, "纠纷裁定成立，悬赏退回，订单已退市");
    }
  } else if (strcmp(verdict, "rejected") == 0) {
    strcpy(d->status, "rejected");
    strcpy(d->verdict, "纠纷驳回，订单恢复到发起前状态继续流转。");
    if (o) {
      o->frozen = 0;
      if (strlen(o->prev_status) > 0)
        strcpy(o->status, o->prev_status);
      add_credit_event(o->creator, "dispute_rejected", "dispute", d->id, 0, -1,
                       -1, "纠纷被驳回，订单恢复流转");
      if (strlen(o->worker) > 0)
        add_credit_event(o->worker, "dispute_rejected", "dispute", d->id, 0, -1,
                         -1, "纠纷被驳回，订单恢复流转");
    }
  } else {
    send_json(client_socket, "400 Bad Request",
              "{\"status\":\"error\",\"message\":\"裁决结果无效\"}");
    return;
  }
  d->resolved_at = (long)time(NULL);

  save_data();
  log_message(LOG_INFO, "Dispute %d arbitrated: %s", dispute_id, verdict);
  send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
}


void handle_request(int client_socket) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);

  if (bytes_read <= 0) {
    close(client_socket);
    return;
  }

  /* 每次请求触发一次时间驱动的自动裁决 / 自动完成检查 */
  run_auto_tasks();

  // Ensure full body for POST
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
          if (n <= 0)
            break;
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
    if (body) {
      body += 4;
      handle_register(client_socket, body);
    }
  } else if (strstr(buffer, "POST /api/login")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_login(client_socket, body);
    }
  } else if (strstr(buffer, "GET /api/orders")) {
    char *path_start = strstr(buffer, "GET /api/orders");
    char *q = strstr(path_start, "?");
    handle_get_orders(client_socket, q);
  } else if (strstr(buffer, "POST /api/orders")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_create_order(client_socket, body);
    }
  } else if (strstr(buffer, "POST /api/update_status")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_update_status(client_socket, body);
    }
  } else if (strstr(buffer, "POST /api/update_profile")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_update_profile(client_socket, body);
    }
  } else if (strstr(buffer, "POST /api/ratings")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_submit_rating(client_socket, body);
    }
  } else if (strstr(buffer, "GET /api/credit_events")) {
    char *path_start = strstr(buffer, "GET /api/credit_events");
    char *q = strstr(path_start, "?");
    handle_get_credit_events(client_socket, q);
  } else if (strstr(buffer, "GET /api/credit")) {
    char *path_start = strstr(buffer, "GET /api/credit");
    char *q = strstr(path_start, "?");
    handle_get_credit(client_socket, q);
  } else if (strstr(buffer, "POST /api/dispute_arbitrate")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_arbitrate_dispute(client_socket, body);
    }
  } else if (strstr(buffer, "POST /api/dispute_statement")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_submit_statement(client_socket, body);
    }
  } else if (strstr(buffer, "POST /api/disputes")) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      handle_create_dispute(client_socket, body);
    }
  } else if (strstr(buffer, "GET /api/disputes")) {
    char *path_start = strstr(buffer, "GET /api/disputes");
    char *q = strstr(path_start, "?");
    handle_list_disputes(client_socket, q);
  } else {
    log_message(LOG_WARN, "404 Not Found: %.50s", buffer);
    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    send(client_socket, response, strlen(response), 0);
  }

  close(client_socket);
}
