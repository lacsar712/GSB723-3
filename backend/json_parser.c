#include "json_parser.h"
#include "database.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void url_decode(char *dst, const char *src) {
  char a, b;
  while (*src) {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a' - 'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a' - 'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16 * a + b;
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

void parse_json_string(const char *body, const char *key, char *output, int max_len) {
  char search[100];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char *p = strstr(body, search);
  if (p) {
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '"')
      p++;
    int i = 0;
    while (*p && *p != '"' && i < max_len - 1)
      output[i++] = *p++;
    output[i] = '\0';
  }
}

int parse_json_int(const char *body, const char *key, int def) {
  char search[100];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char *p = strstr(body, search);
  if (!p) return def;
  p += strlen(search);
  while (*p == ' ' || *p == ':') p++;
  if (*p == '"') p++;
  int val = def;
  if (sscanf(p, "%d", &val) == 1) return val;
  return def;
}

void json_escape(char *dst, const char *src, int max_len) {
  int j = 0;
  for (int i = 0; src[i] && j < max_len - 2; i++) {
    char c = src[i];
    if (c == '"' || c == '\\') {
      if (j < max_len - 3) { dst[j++] = '\\'; dst[j++] = c; }
    } else if (c == '\n' || c == '\r') {
      if (j < max_len - 3) { dst[j++] = '\\'; dst[j++] = 'n'; }
    } else if ((unsigned char)c < 0x20) {
      /* skip control chars */
    } else {
      dst[j++] = c;
    }
  }
  dst[j] = '\0';
}

static void fmt_ts(char *out, int sz, long ts) {
  if (ts <= 0) { out[0] = '\0'; return; }
  time_t t = (time_t)ts;
  struct tm *lt = localtime(&t);
  if (!lt) { out[0] = '\0'; return; }
  strftime(out, sz, "%Y-%m-%d %H:%M", lt);
}

static const char *mask_user(const char *u) {
  (void)u;
  return "同学***";
}

void get_orders_json(char *buf, const char *creator_filter,
                     const char *worker_filter, const char *category_filter) {
  strcat(buf, "[");
  int first = 1;
  char dec_cat[100] = {0};
  if (category_filter && strlen(category_filter) > 0) {
    url_decode(dec_cat, category_filter);
  }

  for (int i = 0; i < order_count; i++) {
    if (creator_filter && strlen(creator_filter) > 0 &&
        strcmp(orders[i].creator, creator_filter) != 0)
      continue;
    if (worker_filter && strlen(worker_filter) > 0 &&
        strcmp(orders[i].worker, worker_filter) != 0)
      continue;
    if (strlen(dec_cat) > 0 && strcmp(dec_cat, "全部") != 0 &&
        strstr(orders[i].category, dec_cat) == NULL &&
        strstr(orders[i].pickup_addr, dec_cat) == NULL)
      continue;

    if (!first) strcat(buf, ",");
    first = 0;

    char esc_pkg[200], esc_pick[200], esc_del[200], esc_rew[40], esc_cat[60],
         esc_cr[60], esc_wk[60], esc_st[30];
    json_escape(esc_pkg, orders[i].package_info, sizeof(esc_pkg));
    json_escape(esc_pick, orders[i].pickup_addr, sizeof(esc_pick));
    json_escape(esc_del, orders[i].delivery_addr, sizeof(esc_del));
    json_escape(esc_rew, orders[i].reward, sizeof(esc_rew));
    json_escape(esc_cat, orders[i].category, sizeof(esc_cat));
    json_escape(esc_cr, orders[i].creator, sizeof(esc_cr));
    json_escape(esc_wk, orders[i].worker, sizeof(esc_wk));
    json_escape(esc_st, orders[i].status, sizeof(esc_st));

    int cr_score = credit_of(orders[i].creator);
    const char *cr_grade = credit_grade(cr_score);
    int cr_low = cr_score < MIN_ACCEPT_CREDIT ? 1 : 0;

    char item[1400];
    snprintf(item, sizeof(item),
             "{\"id\":%d,\"creator\":\"%s\",\"worker\":\"%s\",\"package\":\"%s\","
             "\"pickup\":\"%s\",\"delivery\":\"%s\",\"reward\":\"%s\","
             "\"category\":\"%s\",\"status\":\"%s\",\"frozen\":%d,"
             "\"creatorScore\":%d,\"creatorGrade\":\"%s\",\"creatorLow\":%d,"
             "\"createdAt\":%ld,\"acceptedAt\":%ld,\"deliveredAt\":%ld}",
             orders[i].id, esc_cr, esc_wk, esc_pkg, esc_pick, esc_del, esc_rew,
             esc_cat, esc_st, orders[i].frozen, cr_score, cr_grade, cr_low,
             orders[i].created_at, orders[i].accepted_at, orders[i].delivered_at);
    strcat(buf, item);
  }
  strcat(buf, "]");
}

void get_ratings_json(char *buf, const char *user, const char *direction,
                      int limit) {
  strcat(buf, "[");
  int first = 1;
  int added = 0;
  /* iterate from newest to oldest */
  for (int i = rating_count - 1; i >= 0 && added < limit; i--) {
    Rating *r = &ratings[i];
    int match = 0;
    if (!user || strlen(user) == 0) match = 1;
    else if (direction && strcmp(direction, "given") == 0) {
      if (strcmp(r->from_user, user) == 0) match = 1;
    } else if (direction && strcmp(direction, "received") == 0) {
      if (strcmp(r->to_user, user) == 0) match = 1;
    } else {
      if (strcmp(r->from_user, user) == 0 || strcmp(r->to_user, user) == 0)
        match = 1;
    }
    if (!match) continue;

    if (!first) strcat(buf, ",");
    first = 0;
    added++;

    char esc_from[60], esc_to[60], esc_cmt[120];
    json_escape(esc_from, r->from_user, sizeof(esc_from));
    json_escape(esc_to, r->to_user, sizeof(esc_to));
    json_escape(esc_cmt, r->comment, sizeof(esc_cmt));

    char when[32];
    fmt_ts(when, sizeof(when), r->created_at);

    /* For received ratings, mask the rater; for given, show the rated user */
    char item[1024];
    snprintf(item, sizeof(item),
             "{\"id\":%d,\"orderId\":%d,\"fromUser\":\"%s\",\"toUser\":\"%s\","
             "\"score\":%d,\"comment\":\"%s\",\"createdAt\":%ld,\"time\":\"%s\","
             "\"fromDisplay\":\"%s\",\"toDisplay\":\"%s\"}",
             r->id, r->order_id, esc_from, esc_to, r->score, esc_cmt,
             r->created_at, when,
             (direction && strcmp(direction, "received") == 0) ? mask_user(r->from_user) : esc_from,
             (direction && strcmp(direction, "given") == 0) ? esc_to : esc_to);
    strcat(buf, item);
  }
  strcat(buf, "]");
}

void get_credit_json(char *buf, const char *username) {
  User *u = find_user(username);
  int score = u ? u->credit_score : INITIAL_CREDIT;
  const char *grade = credit_grade(score);
  int can_accept = can_accept_orders(username) ? 1 : 0;

  int given = 0, received = 0;
  for (int i = 0; i < rating_count; i++) {
    if (strcmp(ratings[i].from_user, username) == 0) given++;
    if (strcmp(ratings[i].to_user, username) == 0) received++;
  }

  snprintf(buf + strlen(buf), 400,
           "{\"username\":\"%s\",\"creditScore\":%d,\"grade\":\"%s\","
           "\"canAccept\":%s,\"givenCount\":%d,\"receivedCount\":%d,"
           "\"minAccept\":%d,\"initial\":%d}",
           username, score, grade, can_accept ? "true" : "false",
           given, received, MIN_ACCEPT_CREDIT, INITIAL_CREDIT);
}

void get_credits_json(char *buf) {
  strcat(buf, "[");
  int first = 1;
  for (int i = 0; i < user_count; i++) {
    if (!first) strcat(buf, ",");
    first = 0;
    char item[200];
    snprintf(item, sizeof(item),
             "{\"username\":\"%s\",\"score\":%d,\"grade\":\"%s\",\"canAccept\":%s}",
             users[i].username, users[i].credit_score,
             credit_grade(users[i].credit_score),
             can_accept_orders(users[i].username) ? "true" : "false");
    strcat(buf, item);
  }
  strcat(buf, "]");
}

void get_disputes_json(char *buf, const char *user) {
  strcat(buf, "[");
  int first = 1;
  for (int i = dispute_count - 1; i >= 0; i--) {
    Dispute *d = &disputes[i];
    Order *o = find_order(d->order_id);
    if (user && strlen(user) > 0) {
      int involved = 0;
      if (o) {
        if (strcmp(o->creator, user) == 0 || strcmp(o->worker, user) == 0)
          involved = 1;
      }
      if (strcmp(d->initiator, user) == 0) involved = 1;
      if (!involved) continue;
    }
    if (!first) strcat(buf, ",");
    first = 0;

    char esc_init[60], esc_reason[200], esc_status[20], esc_result[400],
         esc_pkg[200], esc_cr[60], esc_wk[60], esc_prev[30];
    json_escape(esc_init, d->initiator, sizeof(esc_init));
    json_escape(esc_reason, d->reason, sizeof(esc_reason));
    json_escape(esc_status, d->status, sizeof(esc_status));
    json_escape(esc_result, d->result, sizeof(esc_result));
    json_escape(esc_pkg, o ? o->package_info : "", sizeof(esc_pkg));
    json_escape(esc_cr, o ? o->creator : "", sizeof(esc_cr));
    json_escape(esc_wk, o ? o->worker : "", sizeof(esc_wk));
    json_escape(esc_prev, d->prev_status, sizeof(esc_prev));

    char created[32], resolved[32];
    fmt_ts(created, sizeof(created), d->created_at);
    fmt_ts(resolved, sizeof(resolved), d->resolved_at);

    char item[1600];
    snprintf(item, sizeof(item),
             "{\"id\":%d,\"orderId\":%d,\"initiator\":\"%s\",\"reason\":\"%s\","
             "\"status\":\"%s\",\"result\":\"%s\",\"createdAt\":%ld,"
             "\"resolvedAt\":%ld,\"createdTime\":\"%s\",\"resolvedTime\":\"%s\","
             "\"package\":\"%s\",\"creator\":\"%s\",\"worker\":\"%s\","
             "\"reward\":\"%s\",\"prevStatus\":\"%s\","
             "\"hasCreatorResp\":%s,\"hasWorkerResp\":%s,"
             "\"creatorRespAt\":%ld,\"workerRespAt\":%ld}",
             d->id, d->order_id, esc_init, esc_reason, esc_status, esc_result,
             d->created_at, d->resolved_at, created, resolved, esc_pkg, esc_cr,
             esc_wk, o ? o->reward : "", esc_prev,
             strlen(d->creator_resp) > 0 ? "true" : "false",
             strlen(d->worker_resp) > 0 ? "true" : "false",
             d->creator_resp_at, d->worker_resp_at);
    strcat(buf, item);
  }
  strcat(buf, "]");
}

void get_dispute_json(char *buf, const Dispute *d) {
  if (!d) { strcat(buf, "null"); return; }
  Order *o = find_order(d->order_id);
  char esc_init[60], esc_reason[200], esc_status[20], esc_result[500],
      esc_cr[900], esc_wr[900], esc_pkg[200], esc_cr_user[60], esc_wk[60],
      esc_rew[40], esc_prev[30],
      esc_cr_evtype[40], esc_cr_evdesc[260],
      esc_wk_evtype[40], esc_wk_evdesc[260];
  json_escape(esc_init, d->initiator, sizeof(esc_init));
  json_escape(esc_reason, d->reason, sizeof(esc_reason));
  json_escape(esc_status, d->status, sizeof(esc_status));
  json_escape(esc_result, d->result, sizeof(esc_result));
  json_escape(esc_cr, d->creator_resp, sizeof(esc_cr));
  json_escape(esc_wr, d->worker_resp, sizeof(esc_wr));
  json_escape(esc_pkg, o ? o->package_info : "", sizeof(esc_pkg));
  json_escape(esc_cr_user, o ? o->creator : "", sizeof(esc_cr_user));
  json_escape(esc_wk, o ? o->worker : "", sizeof(esc_wk));
  json_escape(esc_rew, o ? o->reward : "", sizeof(esc_rew));
  json_escape(esc_prev, d->prev_status, sizeof(esc_prev));
  json_escape(esc_cr_evtype, d->creator_evidence_type, sizeof(esc_cr_evtype));
  json_escape(esc_cr_evdesc, d->creator_evidence_desc, sizeof(esc_cr_evdesc));
  json_escape(esc_wk_evtype, d->worker_evidence_type, sizeof(esc_wk_evtype));
  json_escape(esc_wk_evdesc, d->worker_evidence_desc, sizeof(esc_wk_evdesc));

  char created[32], resolved[32], cr_at[32], wr_at[32];
  fmt_ts(created, sizeof(created), d->created_at);
  fmt_ts(resolved, sizeof(resolved), d->resolved_at);
  fmt_ts(cr_at, sizeof(cr_at), d->creator_resp_at);
  fmt_ts(wr_at, sizeof(wr_at), d->worker_resp_at);

  snprintf(buf + strlen(buf), 3200,
           "{\"id\":%d,\"orderId\":%d,\"initiator\":\"%s\",\"reason\":\"%s\","
           "\"status\":\"%s\",\"result\":\"%s\",\"createdAt\":%ld,"
           "\"resolvedAt\":%ld,\"createdTime\":\"%s\",\"resolvedTime\":\"%s\","
           "\"package\":\"%s\",\"creator\":\"%s\",\"worker\":\"%s\","
           "\"reward\":\"%s\",\"prevStatus\":\"%s\","
           "\"creatorResp\":\"%s\",\"workerResp\":\"%s\","
           "\"creatorEvidenceType\":\"%s\",\"creatorEvidenceDesc\":\"%s\","
           "\"workerEvidenceType\":\"%s\",\"workerEvidenceDesc\":\"%s\","
           "\"creatorRespAt\":%ld,\"workerRespAt\":%ld,"
           "\"creatorRespTime\":\"%s\",\"workerRespTime\":\"%s\"}",
           d->id, d->order_id, esc_init, esc_reason, esc_status, esc_result,
           d->created_at, d->resolved_at, created, resolved, esc_pkg,
           esc_cr_user, esc_wk, esc_rew, esc_prev, esc_cr, esc_wr,
           esc_cr_evtype, esc_cr_evdesc, esc_wk_evtype, esc_wk_evdesc,
           d->creator_resp_at, d->worker_resp_at, cr_at, wr_at);
}

void get_events_json(char *buf, const char *user, int limit) {
  strcat(buf, "[");
  int first = 1;
  int added = 0;
  if (limit <= 0) limit = 20;
  if (limit > 100) limit = 100;
  for (int i = event_count - 1; i >= 0 && added < limit; i--) {
    CreditEvent *e = &events[i];
    if (user && strlen(user) > 0 && strcmp(e->user, user) != 0) continue;

    if (!first) strcat(buf, ",");
    first = 0;
    added++;

    char esc_type[32], esc_actor[60], esc_detail[400];
    json_escape(esc_type, e->type, sizeof(esc_type));
    json_escape(esc_actor, e->actor, sizeof(esc_actor));
    json_escape(esc_detail, e->detail, sizeof(esc_detail));

    char when[32];
    fmt_ts(when, sizeof(when), e->created_at);

    char item[900];
    snprintf(item, sizeof(item),
             "{\"id\":%d,\"user\":\"%s\",\"type\":\"%s\",\"orderId\":%d,"
             "\"disputeId\":%d,\"ratingId\":%d,\"actor\":\"%s\","
             "\"scoreBefore\":%d,\"scoreAfter\":%d,\"ratingScore\":%d,"
             "\"detail\":\"%s\",\"createdAt\":%ld,\"time\":\"%s\"}",
             e->id, e->user, esc_type, e->order_id, e->dispute_id, e->rating_id,
             esc_actor, e->score_before, e->score_after, e->rating_score,
             esc_detail, e->created_at, when);
    strcat(buf, item);
  }
  strcat(buf, "]");
}
