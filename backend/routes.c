#include "routes.h"
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

static void send_json(int client_socket, const char *code, const char *body) {
  char header[256];
  int body_len = (int)strlen(body);
  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\nContent-Type: application/json; charset=UTF-8\r\n"
           "Content-Length: %d\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
           code, body_len);
  send(client_socket, header, strlen(header), 0);
  if (body_len > 0) send(client_socket, body, body_len, 0);
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

static void send_error(int client_socket, const char *code,
                       const char *message) {
  char body[256];
  snprintf(body, sizeof(body),
           "{\"status\":\"error\",\"message\":\"%s\"}", message ? message : "");
  send_json(client_socket, code, body);
}

static const char *query_param(const char *q, const char *name) {
  static char buf[256];
  buf[0] = '\0';
  if (!q) return buf;
  char search[64];
  snprintf(search, sizeof(search), "%s=", name);
  char *p = strstr(q, search);
  if (!p) return buf;
  p += strlen(search);
  int i = 0;
  while (*p && *p != '&' && *p != ' ' && i < (int)sizeof(buf) - 1)
    buf[i++] = *p++;
  buf[i] = '\0';
  return buf;
}

static void handle_register(int client_socket, char *body) {
  User u;
  memset(&u, 0, sizeof(User));
  u.credit_score = INITIAL_CREDIT;

  parse_json_string(body, "username", u.username, sizeof(u.username));
  parse_json_string(body, "password", u.password, sizeof(u.password));
  parse_json_string(body, "realName", u.real_name, sizeof(u.real_name));
  parse_json_string(body, "major", u.major, sizeof(u.major));

  if (strlen(u.username) == 0 || strlen(u.password) == 0) {
    send_error(client_socket, "400 Bad Request", "缺少账号或密码");
    return;
  }

  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, u.username) == 0) {
      send_error(client_socket, "409 Conflict", "用户名已存在");
      return;
    }
  }

  if (user_count < MAX_USERS) {
    users[user_count++] = u;
    save_data();
    log_message(LOG_INFO, "User registered: %s", u.username);

    char resp[600];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"username\":\"%s\",\"realName\":\"%s\","
             "\"major\":\"%s\",\"creditScore\":%d,\"grade\":\"%s\"}",
             u.username, u.real_name, u.major, u.credit_score,
             credit_grade(u.credit_score));
    send_json(client_socket, "200 OK", resp);
  } else {
    send_error(client_socket, "500 Internal Server Error", "用户数已达上限");
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
    char resp[600];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"username\":\"%s\",\"realName\":\"%s\","
             "\"major\":\"%s\",\"creditScore\":%d,\"grade\":\"%s\","
             "\"canAccept\":%s}",
             users[found].username, users[found].real_name, users[found].major,
             users[found].credit_score, credit_grade(users[found].credit_score),
             can_accept_orders(users[found].username) ? "true" : "false");
    send_json(client_socket, "200 OK", resp);
    log_message(LOG_INFO, "User logged in: %s (credit %d)", user,
                users[found].credit_score);
  } else {
    send_error(client_socket, "401 Unauthorized", "账号或密码错误");
  }
}

static void handle_get_orders(int client_socket, char *query_string) {
  char creator[80] = "", worker[80] = "", category[80] = "";
  if (query_string) {
    const char *v;
    v = query_param(query_string, "creator");
    if (v) url_decode(creator, v);
    v = query_param(query_string, "worker");
    if (v) url_decode(worker, v);
    v = query_param(query_string, "category");
    if (v) url_decode(category, v);
  }

  char *json = malloc(MAX_ORDERS * 1500);
  if (!json) {
    send_error(client_socket, "500 Internal Server Error", "内存不足");
    return;
  }
  memset(json, 0, MAX_ORDERS * 1500);
  get_orders_json(json, creator, worker, category);
  send_json(client_socket, "200 OK", json);
  free(json);
}

static void handle_create_order(int client_socket, char *body) {
  Order new_order;
  memset(&new_order, 0, sizeof(Order));
  new_order.id = next_id++;
  new_order.created_at = (long)time(NULL);

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

  if (strlen(new_order.creator) == 0) {
    send_error(client_socket, "400 Bad Request", "未登录");
    return;
  }

  if (order_count < MAX_ORDERS) {
    orders[order_count++] = new_order;
    save_data();
    log_message(LOG_INFO, "Order created: ID=%d by %s", new_order.id,
                new_order.creator);
    send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
  } else {
    send_error(client_socket, "500 Internal Server Error", "订单数已达上限");
  }
}

static void handle_update_status(int client_socket, char *body) {
  int id = parse_json_int(body, "id", -1);
  char new_status[20] = "", worker[50] = "";
  parse_json_string(body, "status", new_status, sizeof(new_status));
  parse_json_string(body, "worker", worker, sizeof(worker));

  Order *o = find_order(id);
  if (!o) {
    send_error(client_socket, "404 Not Found", "订单不存在");
    return;
  }

  if (o->frozen && strcmp(new_status, "disputed") != 0) {
    send_error(client_socket, "409 Conflict", "订单已冻结，存在未裁决的纠纷");
    return;
  }

  if (strcmp(new_status, "accepted") == 0) {
    if (strlen(o->worker) > 0 && strcmp(o->worker, worker) != 0) {
      send_error(client_socket, "409 Conflict", "该订单已被他人接单");
      return;
    }
    if (strlen(worker) > 0 && !can_accept_orders(worker)) {
      send_error(client_socket, "403 Forbidden", "您的信用分过低，暂时无法接单");
      return;
    }
    if (strlen(worker) > 0) strcpy(o->worker, worker);
    o->accepted_at = (long)time(NULL);
  } else if (strcmp(new_status, "delivered") == 0) {
    if (strlen(worker) > 0 && strlen(o->worker) == 0) strcpy(o->worker, worker);
    o->delivered_at = (long)time(NULL);
  } else if (strcmp(new_status, "completed") == 0) {
    if (strcmp(o->status, "delivered") != 0 && strcmp(o->status, "accepted") != 0) {
      send_error(client_socket, "409 Conflict", "当前状态无法完成");
      return;
    }
  } else if (strcmp(new_status, "cancelled") == 0) {
    if (strcmp(o->status, "pending") != 0) {
      send_error(client_socket, "409 Conflict", "仅待接单订单可撤回");
      return;
    }
  }

  strcpy(o->status, new_status);
  save_data();
  log_message(LOG_INFO, "Order %d -> %s", id, new_status);
  send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
}

static void handle_update_profile(int client_socket, char *body) {
  char username[50] = "", real_name[50] = "", major[50] = "", pwd[50] = "";
  parse_json_string(body, "username", username, sizeof(username));
  parse_json_string(body, "realName", real_name, sizeof(real_name));
  parse_json_string(body, "major", major, sizeof(major));
  parse_json_string(body, "password", pwd, sizeof(pwd));

  User *u = find_user(username);
  if (!u) {
    send_error(client_socket, "404 Not Found", "用户不存在");
    return;
  }
  if (strlen(real_name) > 0) strcpy(u->real_name, real_name);
  if (strlen(major) > 0) strcpy(u->major, major);
  if (strlen(pwd) > 0) strcpy(u->password, pwd);
  save_data();
  send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
}

static void handle_submit_rating(int client_socket, char *body) {
  int order_id = parse_json_int(body, "orderId", -1);
  int score = parse_json_int(body, "score", 0);
  char from_user[50] = "", comment[60] = "";
  parse_json_string(body, "from", from_user, sizeof(from_user));
  parse_json_string(body, "comment", comment, sizeof(comment));

  if (score < 1 || score > 5) {
    send_error(client_socket, "400 Bad Request", "评分需在 1-5 之间");
    return;
  }
  Order *o = find_order(order_id);
  if (!o) {
    send_error(client_socket, "404 Not Found", "订单不存在");
    return;
  }
  if (strcmp(o->status, "completed") != 0 && strcmp(o->status, "refunded") != 0) {
    send_error(client_socket, "409 Conflict", "订单尚未完成，无法评价");
    return;
  }

  int is_creator = (strcmp(o->creator, from_user) == 0);
  int is_worker = (strcmp(o->worker, from_user) == 0);
  if (!is_creator && !is_worker) {
    send_error(client_socket, "403 Forbidden", "您不是本订单的参与方");
    return;
  }
  if (has_rating(order_id, from_user)) {
    send_error(client_socket, "409 Conflict", "您已评价过该订单");
    return;
  }

  Rating r;
  memset(&r, 0, sizeof(r));
  r.id = next_rating_id++;
  r.order_id = order_id;
  strcpy(r.from_user, from_user);
  strcpy(r.to_user, is_creator ? o->worker : o->creator);
  if (strlen(r.to_user) == 0) {
    send_error(client_socket, "409 Conflict", "对方信息缺失，无法评价");
    return;
  }
  r.score = score;
  strncpy(r.comment, comment, sizeof(r.comment) - 1);
  r.created_at = (long)time(NULL);

  if (rating_count < MAX_RATINGS) {
    User *target = find_user(r.to_user);
    int old_score = target ? target->credit_score : INITIAL_CREDIT;
    ratings[rating_count++] = r;
    apply_rating_credit(&ratings[rating_count - 1]);
    target = find_user(r.to_user);
    int new_score = target ? target->credit_score : old_score;

    char detail_given[260], detail_received[260], detail_change[260];
    snprintf(detail_given, sizeof(detail_given), "我对订单 #%d 给出 %d 星评价%s%s",
             order_id, score, strlen(comment) ? "：" : "", comment);
    snprintf(detail_received, sizeof(detail_received),
             "收到关于订单 #%d 的 %d 星评价%s%s",
             order_id, score, strlen(comment) ? "：" : "", comment);
    snprintf(detail_change, sizeof(detail_change),
             "因订单 #%d 收到 %d 星评价，信用分 %d → %d",
             order_id, score, old_score, new_score);

    add_event(r.from_user, "rating_given", order_id, 0, r.id, r.to_user,
              0, 0, score, detail_given);
    add_event(r.to_user, "rating_received", order_id, 0, r.id, r.from_user,
              0, 0, score, detail_received);
    if (new_score != old_score) {
      add_event(r.to_user, "credit_change", order_id, 0, r.id, r.from_user,
                old_score, new_score, score, detail_change);
    }

    save_data();
    char resp[300];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"newScore\":%d,\"grade\":\"%s\","
             "\"oldScore\":%d}",
             new_score, credit_grade(new_score), old_score);
    send_json(client_socket, "200 OK", resp);
    log_message(LOG_INFO, "Rating: %s -> %s score %d on order %d (%d->%d)",
                from_user, r.to_user, score, order_id, old_score, new_score);
  } else {
    send_error(client_socket, "500 Internal Server Error", "评价数已达上限");
  }
}

static void handle_get_ratings(int client_socket, char *query_string) {
  char user[60] = "", direction[30] = "";
  if (query_string) {
    const char *v;
    v = query_param(query_string, "user");
    if (v) url_decode(user, v);
    v = query_param(query_string, "filter");
    if (v) strncpy(direction, v, sizeof(direction) - 1);
  }
  int limit = parse_json_int(query_string ? query_string : "", "limit", 10);
  if (limit <= 0) limit = 10;
  if (limit > 50) limit = 50;

  char *json = malloc(MAX_RATINGS * 1024);
  if (!json) {
    send_error(client_socket, "500 Internal Server Error", "内存不足");
    return;
  }
  memset(json, 0, MAX_RATINGS * 1024);
  get_ratings_json(json, user, direction, limit);
  send_json(client_socket, "200 OK", json);
  free(json);
}

static void handle_get_credit(int client_socket, char *query_string) {
  char user[60] = "";
  if (query_string) {
    const char *v = query_param(query_string, "user");
    if (v) url_decode(user, v);
  }
  if (strlen(user) == 0) {
    send_error(client_socket, "400 Bad Request", "缺少 user 参数");
    return;
  }
  if (!find_user(user)) {
    send_error(client_socket, "404 Not Found", "用户不存在");
    return;
  }
  char buf[600];
  memset(buf, 0, sizeof(buf));
  get_credit_json(buf, user);
  send_json(client_socket, "200 OK", buf);
}

static void handle_get_credits(int client_socket) {
  char *json = malloc(MAX_USERS * 220);
  if (!json) {
    send_error(client_socket, "500 Internal Server Error", "内存不足");
    return;
  }
  memset(json, 0, MAX_USERS * 220);
  get_credits_json(json);
  send_json(client_socket, "200 OK", json);
  free(json);
}

static void handle_create_dispute(int client_socket, char *body) {
  int order_id = parse_json_int(body, "orderId", -1);
  char initiator[50] = "", reason[110] = "";
  parse_json_string(body, "initiator", initiator, sizeof(initiator));
  parse_json_string(body, "reason", reason, sizeof(reason));

  Order *o = find_order(order_id);
  if (!o) {
    send_error(client_socket, "404 Not Found", "订单不存在");
    return;
  }
  if (strcmp(o->status, "accepted") != 0 &&
      strcmp(o->status, "delivered") != 0) {
    send_error(client_socket, "409 Conflict", "当前订单状态不可发起纠纷");
    return;
  }
  int involved = (strcmp(o->creator, initiator) == 0 ||
                  (strlen(o->worker) && strcmp(o->worker, initiator) == 0));
  if (!involved) {
    send_error(client_socket, "403 Forbidden", "您不是本订单的参与方");
    return;
  }
  Dispute *exist = find_dispute_by_order(order_id);
  if (exist) {
    send_error(client_socket, "409 Conflict", "该订单已存在纠纷单");
    return;
  }
  if (strlen(reason) == 0) {
    send_error(client_socket, "400 Bad Request", "请填写纠纷原因");
    return;
  }

  Dispute d;
  memset(&d, 0, sizeof(d));
  d.id = next_dispute_id++;
  d.order_id = order_id;
  strcpy(d.initiator, initiator);
  strncpy(d.reason, reason, sizeof(d.reason) - 1);
  strcpy(d.status, "pending");
  strcpy(d.prev_status, o->status);
  d.created_at = (long)time(NULL);

  if (dispute_count < MAX_DISPUTES) {
    disputes[dispute_count++] = d;
    o->frozen = 1;
    strcpy(o->status, "disputed");

    char detail[300];
    snprintf(detail, sizeof(detail), "订单 #%d 被发起纠纷：%s", order_id, reason);
    add_event(o->creator, "dispute_filed", order_id, d.id, 0, initiator,
              0, 0, 0, detail);
    if (strlen(o->worker) > 0) {
      add_event(o->worker, "dispute_filed", order_id, d.id, 0, initiator,
                0, 0, 0, detail);
    }

    save_data();
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"disputeId\":%d,\"prevStatus\":\"%s\"}",
             d.id, d.prev_status);
    send_json(client_socket, "200 OK", resp);
    log_message(LOG_WARN, "Dispute %d filed on order %d by %s", d.id, order_id,
                initiator);
  } else {
    send_error(client_socket, "500 Internal Server Error", "纠纷单已达上限");
  }
}

static void handle_get_disputes(int client_socket, char *query_string) {
  char user[60] = "";
  int id = -1;
  if (query_string) {
    const char *v;
    v = query_param(query_string, "user");
    if (v) url_decode(user, v);
    const char *idstr = query_param(query_string, "id");
    if (idstr && strlen(idstr) > 0) id = atoi(idstr);
  }

  char *json = malloc(MAX_DISPUTES * 2000);
  if (!json) {
    send_error(client_socket, "500 Internal Server Error", "内存不足");
    return;
  }
  memset(json, 0, MAX_DISPUTES * 2000);

  if (id > 0) {
    Dispute *d = find_dispute(id);
    if (!d) {
      free(json);
      send_error(client_socket, "404 Not Found", "纠纷单不存在");
      return;
    }
    get_dispute_json(json, d);
  } else {
    get_disputes_json(json, user);
  }
  send_json(client_socket, "200 OK", json);
  free(json);
}

static void handle_respond_dispute(int client_socket, char *body) {
  int id = parse_json_int(body, "id", -1);
  char user[50] = "", response[700] = "", ev_type[32] = "", ev_desc[220] = "";
  parse_json_string(body, "user", user, sizeof(user));
  parse_json_string(body, "response", response, sizeof(response));
  parse_json_string(body, "evidenceType", ev_type, sizeof(ev_type));
  parse_json_string(body, "evidenceDesc", ev_desc, sizeof(ev_desc));

  Dispute *d = find_dispute(id);
  if (!d) {
    send_error(client_socket, "404 Not Found", "纠纷单不存在");
    return;
  }
  if (strcmp(d->status, "pending") != 0) {
    send_error(client_socket, "409 Conflict", "纠纷已裁决，无法补充");
    return;
  }
  Order *o = find_order(d->order_id);
  if (!o) {
    send_error(client_socket, "404 Not Found", "关联订单丢失");
    return;
  }
  long now = (long)time(NULL);
  if (strcmp(o->creator, user) == 0) {
    if (strlen(d->creator_resp) > 0) {
      send_error(client_socket, "409 Conflict", "发布方已提交过补充说明");
      return;
    }
    strncpy(d->creator_resp, response, sizeof(d->creator_resp) - 1);
    strncpy(d->creator_evidence_type, ev_type, sizeof(d->creator_evidence_type) - 1);
    strncpy(d->creator_evidence_desc, ev_desc, sizeof(d->creator_evidence_desc) - 1);
    d->creator_resp_at = now;
  } else if (strlen(o->worker) && strcmp(o->worker, user) == 0) {
    if (strlen(d->worker_resp) > 0) {
      send_error(client_socket, "409 Conflict", "接单方已提交过补充说明");
      return;
    }
    strncpy(d->worker_resp, response, sizeof(d->worker_resp) - 1);
    strncpy(d->worker_evidence_type, ev_type, sizeof(d->worker_evidence_type) - 1);
    strncpy(d->worker_evidence_desc, ev_desc, sizeof(d->worker_evidence_desc) - 1);
    d->worker_resp_at = now;
  } else {
    send_error(client_socket, "403 Forbidden", "您不是本纠纷的参与方");
    return;
  }
  save_data();
  send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
  log_message(LOG_INFO, "Dispute %d response by %s (ev=%s)", id, user, ev_type);
}

static void handle_rule_dispute(int client_socket, char *body) {
  int id = parse_json_int(body, "id", -1);
  char verdict[20] = "", note[200] = "";
  parse_json_string(body, "verdict", verdict, sizeof(verdict));
  parse_json_string(body, "note", note, sizeof(note));

  Dispute *d = find_dispute(id);
  if (!d) {
    send_error(client_socket, "404 Not Found", "纠纷单不存在");
    return;
  }
  if (strcmp(d->status, "pending") != 0) {
    send_error(client_socket, "409 Conflict", "纠纷已裁决");
    return;
  }

  int upheld = -1;
  char txt[512];
  if (strcmp(verdict, "upheld") == 0) {
    upheld = 1;
    snprintf(txt, sizeof(txt), "%s%s",
             strlen(note) ? note : "纠纷成立：判定接单方责任，悬赏金额退回发布方。",
             "（系统已回写订单为已退款终态）");
  } else if (strcmp(verdict, "rejected") == 0) {
    upheld = 0;
    snprintf(txt, sizeof(txt), "%s",
             strlen(note) ? note : "纠纷驳回：证据不足，恢复原订单状态继续流转。");
  } else {
    send_error(client_socket, "400 Bad Request", "verdict 需为 upheld 或 rejected");
    return;
  }

  rule_dispute(d, upheld, txt);
  save_data();
  send_json(client_socket, "200 OK", "{\"status\":\"success\"}");
  log_message(LOG_WARN, "Dispute %d manually ruled: %s", id, verdict);
}

static void handle_get_events(int client_socket, char *query_string) {
  char user[60] = "";
  int limit = 20;
  if (query_string) {
    const char *v = query_param(query_string, "user");
    if (v) url_decode(user, v);
    const char *l = query_param(query_string, "limit");
    if (l && strlen(l) > 0) {
      int parsed = atoi(l);
      if (parsed > 0) limit = parsed;
    }
  }
  if (limit <= 0) limit = 20;
  if (limit > 100) limit = 100;

  char *json = malloc(MAX_EVENTS * 900 + 16);
  if (!json) {
    send_error(client_socket, "500 Internal Server Error", "内存不足");
    return;
  }
  memset(json, 0, MAX_EVENTS * 900 + 16);
  get_events_json(json, user, limit);
  send_json(client_socket, "200 OK", json);
  free(json);
}

void handle_request(int client_socket) {
  run_auto_settlement();

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);

  if (bytes_read <= 0) {
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

  char *body = strstr(buffer, "\r\n\r\n");
  if (body) body += 4;

  if (strstr(buffer, "OPTIONS ")) {
    char resp[] =
        "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n\r\n";
    send(client_socket, resp, strlen(resp), 0);
  } else if (strncmp(buffer, "GET / ", 6) == 0 || strncmp(buffer, "GET /?", 6) == 0 ||
             strncmp(buffer, "GET /\r\n", 6) == 0 ||
             strstr(buffer, "GET /index.html")) {
    send_file(client_socket, "frontend/index.html", "text/html; charset=UTF-8");
  } else if (strstr(buffer, "GET /style.css")) {
    send_file(client_socket, "frontend/css/style.css", "text/css");
  } else if (strstr(buffer, "GET /credit.css")) {
    send_file(client_socket, "frontend/css/credit.css", "text/css");
  } else if (strstr(buffer, "GET /script.js")) {
    send_file(client_socket, "frontend/js/script.js", "application/javascript");
  } else if (strstr(buffer, "GET /credit-api.js")) {
    send_file(client_socket, "frontend/js/credit-api.js",
              "application/javascript");
  } else if (strstr(buffer, "GET /credit-ui.js")) {
    send_file(client_socket, "frontend/js/credit-ui.js",
              "application/javascript");
  } else if (strstr(buffer, "POST /api/register")) {
    handle_register(client_socket, body);
  } else if (strstr(buffer, "POST /api/login")) {
    handle_login(client_socket, body);
  } else if (strstr(buffer, "GET /api/orders")) {
    char *path_start = strstr(buffer, "GET /api/orders");
    char *q = strstr(path_start, "?");
    handle_get_orders(client_socket, q);
  } else if (strstr(buffer, "POST /api/orders")) {
    handle_create_order(client_socket, body);
  } else if (strstr(buffer, "POST /api/update_status")) {
    handle_update_status(client_socket, body);
  } else if (strstr(buffer, "POST /api/update_profile")) {
    handle_update_profile(client_socket, body);
  } else if (strstr(buffer, "POST /api/ratings")) {
    handle_submit_rating(client_socket, body);
  } else if (strstr(buffer, "GET /api/ratings")) {
    char *path_start = strstr(buffer, "GET /api/ratings");
    char *q = strstr(path_start, "?");
    handle_get_ratings(client_socket, q);
  } else if (strstr(buffer, "GET /api/credits")) {
    handle_get_credits(client_socket);
  } else if (strstr(buffer, "GET /api/credit")) {
    char *path_start = strstr(buffer, "GET /api/credit");
    char *q = strstr(path_start, "?");
    handle_get_credit(client_socket, q);
  } else if (strstr(buffer, "POST /api/disputes")) {
    handle_create_dispute(client_socket, body);
  } else if (strstr(buffer, "GET /api/disputes")) {
    char *path_start = strstr(buffer, "GET /api/disputes");
    char *q = strstr(path_start, "?");
    handle_get_disputes(client_socket, q);
  } else if (strstr(buffer, "GET /api/events")) {
    char *path_start = strstr(buffer, "GET /api/events");
    char *q = strstr(path_start, "?");
    handle_get_events(client_socket, q);
  } else if (strstr(buffer, "POST /api/dispute/respond")) {
    handle_respond_dispute(client_socket, body);
  } else if (strstr(buffer, "POST /api/dispute/rule")) {
    handle_rule_dispute(client_socket, body);
  } else {
    log_message(LOG_WARN, "404 Not Found: %.50s", buffer);
    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    send(client_socket, response, strlen(response), 0);
  }

  close(client_socket);
}
