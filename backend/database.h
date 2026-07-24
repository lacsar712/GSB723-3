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

User *find_user(const char *username);
Order *find_order(int id);
int credit_of(const char *username);
const char *credit_grade(int score);
int can_accept_orders(const char *username);
void apply_rating_credit(Rating *r);
int has_rating(int order_id, const char *from_user);
Dispute *find_dispute_by_order(int order_id);
Dispute *find_dispute(int id);

void run_auto_settlement();

#endif
