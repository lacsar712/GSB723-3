#ifndef DATABASE_H
#define DATABASE_H

#include "types.h"

extern User users[MAX_USERS];
extern int user_count;
extern Order orders[MAX_ORDERS];
extern int order_count;
extern int next_id;
extern Rating ratings[MAX_RATINGS];
extern int rating_count;
extern int next_rating_id;
extern Dispute disputes[MAX_DISPUTES];
extern int dispute_count;
extern int next_dispute_id;

void save_data();
void load_data();
User* find_user(const char *username);
Order* find_order(int id);
int compute_credit_level(int score);
const char* credit_level_label(int level);
void update_credit_score(const char *username, int new_score);

#endif
