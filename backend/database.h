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
extern CreditEvent events[MAX_EVENTS];
extern int event_count;
extern int next_event_id;
extern DisputeStatement statements[MAX_STATEMENTS];
extern int statement_count;
extern int next_statement_id;

void save_data();
void load_data();

#endif
