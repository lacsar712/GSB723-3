#include "json_parser.h"
#include "database.h"
#include <ctype.h>
#include <stdio.h>
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
  }
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

    if (!first)
      strcat(buf, ",");
    char item[1024];
    sprintf(item,
            "{\"id\":%d,\"creator\":\"%s\",\"worker\":\"%s\",\"package\":\"%"
            "s\",\"pickup\":\"%s\",\"delivery\":\"%s\",\"reward\":\"%s\","
            "\"category\":\"%s\",\"status\":\"%s\"}",
            orders[i].id, orders[i].creator,
            (strlen(orders[i].worker) > 0 ? orders[i].worker : ""),
            orders[i].package_info, orders[i].pickup_addr,
            orders[i].delivery_addr, orders[i].reward, orders[i].category,
            orders[i].status);
    strcat(buf, item);
    first = 0;
  }
  strcat(buf, "]");
}
