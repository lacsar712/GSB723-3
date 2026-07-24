#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "types.h"

void url_decode(char *dst, const char *src);
void parse_json_string(const char *body, const char *key, char *output, int max_len);
int parse_json_int(const char *body, const char *key, int default_val);
long long parse_json_long(const char *body, const char *key, long long default_val);
void escape_json_string(char *dst, const char *src, int max_len);
void get_orders_json(char *buf, const char *creator_filter, const char *worker_filter, const char *category_filter);
void get_ratings_json(char *buf, const char *username, const char *direction, int limit);
void get_disputes_json(char *buf, const char *username);
void get_dispute_json(char *buf, Dispute *d);

#endif
