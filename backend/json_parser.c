#include "json_parser.h"
#include "database.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  } else {
    output[0] = '\0';
  }
}

int parse_json_int(const char *body, const char *key, int default_val) {
  char search[100];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char *p = strstr(body, search);
  if (p) {
    p += strlen(search);
    while (*p == ' ' || *p == ':')
      p++;
    if (*p == '"') p++;
    return atoi(p);
  }
  return default_val;
}

long long parse_json_long(const char *body, const char *key, long long default_val) {
  char search[100];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char *p = strstr(body, search);
  if (p) {
    p += strlen(search);
    while (*p == ' ' || *p == ':')
      p++;
    if (*p == '"') p++;
    return atoll(p);
  }
  return default_val;
}

void escape_json_string(char *dst, const char *src, int max_len) {
  int i = 0;
  while (*src && i < max_len - 2) {
    if (*src == '"' || *src == '\\') {
      dst[i++] = '\\';
      dst[i++] = *src;
    } else if (*src == '\n') {
      dst[i++] = '\\';
      dst[i++] = 'n';
    } else if (*src == '\r') {
      dst[i++] = '\\';
      dst[i++] = 'r';
    } else if (*src == '\t') {
      dst[i++] = '\\';
      dst[i++] = 't';
    } else {
      dst[i++] = *src;
    }
    src++;
  }
  dst[i] = '\0';
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

    User *creator_u = find_user(orders[i].creator);
    int creator_credit = creator_u ? creator_u->credit_score : 100;
    int creator_level = compute_credit_level(creator_credit);
    const char *creator_label = credit_level_label(creator_level);

    User *worker_u = NULL;
    int worker_credit = 100;
    int worker_level = 0;
    const char *worker_label = "优";
    if (strlen(orders[i].worker) > 0) {
      worker_u = find_user(orders[i].worker);
      worker_credit = worker_u ? worker_u->credit_score : 100;
      worker_level = compute_credit_level(worker_credit);
      worker_label = credit_level_label(worker_level);
    }

    if (!first)
      strcat(buf, ",");
    char item[2048];
    snprintf(item, sizeof(item),
            "{\"id\":%d,\"creator\":\"%s\",\"worker\":\"%s\",\"package\":\"%s\","
            "\"pickup\":\"%s\",\"delivery\":\"%s\",\"reward\":\"%s\","
            "\"category\":\"%s\",\"status\":\"%s\","
            "\"creatorCredit\":%d,\"creatorLevel\":\"%s\","
            "\"workerCredit\":%d,\"workerLevel\":\"%s\","
            "\"frozen\":%s,\"creatorRated\":%s,\"workerRated\":%s,"
            "\"disputed\":%s,\"createdAt\":%lld,\"acceptedAt\":%lld,"
            "\"deliveredAt\":%lld,\"completedAt\":%lld}",
            orders[i].id, orders[i].creator,
            (strlen(orders[i].worker) > 0 ? orders[i].worker : ""),
            orders[i].package_info, orders[i].pickup_addr,
            orders[i].delivery_addr, orders[i].reward, orders[i].category,
            orders[i].status,
            creator_credit, creator_label,
            worker_credit, worker_label,
            orders[i].frozen ? "true" : "false",
            orders[i].creator_rated ? "true" : "false",
            orders[i].worker_rated ? "true" : "false",
            orders[i].disputed ? "true" : "false",
            orders[i].created_at, orders[i].accepted_at,
            orders[i].delivered_at, orders[i].completed_at);
    strcat(buf, item);
    first = 0;
  }
  strcat(buf, "]");
}

void get_ratings_json(char *buf, const char *username, const char *direction, int limit) {
  strcat(buf, "[");
  int first = 1;
  int count = 0;
  for (int i = rating_count - 1; i >= 0; i--) {
    int include = 0;
    if (direction && strcmp(direction, "given") == 0) {
      if (strcmp(ratings[i].rater, username) == 0) include = 1;
    } else if (direction && strcmp(direction, "received") == 0) {
      if (strcmp(ratings[i].ratee, username) == 0) include = 1;
    } else {
      if (strcmp(ratings[i].rater, username) == 0 ||
          strcmp(ratings[i].ratee, username) == 0)
        include = 1;
    }
    if (!include) continue;
    if (limit > 0 && count >= limit) break;

    if (!first) strcat(buf, ",");
    char masked_rater[60];
    char masked_ratee[60];
    char esc_comment[200];

    if (strcmp(ratings[i].rater, username) == 0) {
      snprintf(masked_rater, sizeof(masked_rater), "%s", ratings[i].rater);
    } else {
      int len = (int)strlen(ratings[i].rater);
      if (len <= 1) {
        snprintf(masked_rater, sizeof(masked_rater), "%c*", ratings[i].rater[0]);
      } else {
        snprintf(masked_rater, sizeof(masked_rater), "%c%s", ratings[i].rater[0],
                 len > 2 ? "**" : "*");
      }
    }

    if (strcmp(ratings[i].ratee, username) == 0) {
      snprintf(masked_ratee, sizeof(masked_ratee), "%s", ratings[i].ratee);
    } else {
      int len = (int)strlen(ratings[i].ratee);
      if (len <= 1) {
        snprintf(masked_ratee, sizeof(masked_ratee), "%c*", ratings[i].ratee[0]);
      } else {
        snprintf(masked_ratee, sizeof(masked_ratee), "%c%s", ratings[i].ratee[0],
                 len > 2 ? "**" : "*");
      }
    }

    escape_json_string(esc_comment, ratings[i].comment, sizeof(esc_comment));

    char item[1024];
    snprintf(item, sizeof(item),
             "{\"id\":%d,\"orderId\":%d,\"rater\":\"%s\",\"ratee\":\"%s\","
             "\"score\":%d,\"comment\":\"%s\",\"createdAt\":%lld,"
             "\"raterMasked\":\"%s\",\"rateeMasked\":\"%s\"}",
             ratings[i].id, ratings[i].order_id, ratings[i].rater,
             ratings[i].ratee, ratings[i].score, esc_comment,
             ratings[i].created_at, masked_rater, masked_ratee);
    strcat(buf, item);
    first = 0;
    count++;
  }
  strcat(buf, "]");
}

void get_dispute_json(char *buf, Dispute *d) {
  char esc_reason[400];
  char esc_resolution[400];
  escape_json_string(esc_reason, d->reason, sizeof(esc_reason));
  escape_json_string(esc_resolution, d->resolution, sizeof(esc_resolution));
  snprintf(buf + strlen(buf), 2048,
           "{\"id\":%d,\"orderId\":%d,\"initiator\":\"%s\",\"reason\":\"%s\","
           "\"status\":\"%s\",\"createdAt\":%lld,\"resolvedAt\":%lld,"
           "\"resolution\":\"%s\",\"prevOrderStatus\":\"%s\"}",
           d->id, d->order_id, d->initiator, esc_reason, d->status,
           d->created_at, d->resolved_at, esc_resolution, d->prev_order_status);
}

void get_disputes_json(char *buf, const char *username) {
  strcat(buf, "[");
  int first = 1;
  for (int i = 0; i < dispute_count; i++) {
    if (username && strlen(username) > 0) {
      Order *o = find_order(disputes[i].order_id);
      if (!o) continue;
      if (strcmp(o->creator, username) != 0 &&
          strcmp(o->worker, username) != 0)
        continue;
    }
    if (!first) strcat(buf, ",");
    char item[2048];
    item[0] = '\0';
    get_dispute_json(item, &disputes[i]);
    strcat(buf, item);
    first = 0;
  }
  strcat(buf, "]");
}
