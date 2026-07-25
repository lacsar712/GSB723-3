#include "database.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

User users[MAX_USERS];
int user_count = 0;
Order orders[MAX_ORDERS];
int order_count = 0;
int next_id = 1;
Rating ratings[MAX_RATINGS];
int rating_count = 0;
int next_rating_id = 1;
Dispute disputes[MAX_DISPUTES];
int dispute_count = 0;
int next_dispute_id = 1;
CreditEvent events[MAX_EVENTS];
int event_count = 0;
int next_event_id = 1;
DisputeStatement statements[MAX_STATEMENTS];
int statement_count = 0;
int next_statement_id = 1;

void save_data() {
  FILE *f1 = fopen("data_orders.bin", "wb");
  if (f1) {
    fwrite(&order_count, sizeof(int), 1, f1);
    fwrite(&next_id, sizeof(int), 1, f1);
    fwrite(orders, sizeof(Order), order_count, f1);
    fclose(f1);
    log_message(LOG_INFO, "Orders data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save orders data");
  }

  FILE *f2 = fopen("data_users.bin", "wb");
  if (f2) {
    fwrite(&user_count, sizeof(int), 1, f2);
    fwrite(users, sizeof(User), user_count, f2);
    fclose(f2);
    log_message(LOG_INFO, "Users data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save users data");
  }

  FILE *f3 = fopen("data_ratings.bin", "wb");
  if (f3) {
    fwrite(&rating_count, sizeof(int), 1, f3);
    fwrite(&next_rating_id, sizeof(int), 1, f3);
    fwrite(ratings, sizeof(Rating), rating_count, f3);
    fclose(f3);
    log_message(LOG_INFO, "Ratings data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save ratings data");
  }

  FILE *f4 = fopen("data_disputes.bin", "wb");
  if (f4) {
    fwrite(&dispute_count, sizeof(int), 1, f4);
    fwrite(&next_dispute_id, sizeof(int), 1, f4);
    fwrite(disputes, sizeof(Dispute), dispute_count, f4);
    fclose(f4);
    log_message(LOG_INFO, "Disputes data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save disputes data");
  }

  FILE *f5 = fopen("data_events.bin", "wb");
  if (f5) {
    fwrite(&event_count, sizeof(int), 1, f5);
    fwrite(&next_event_id, sizeof(int), 1, f5);
    fwrite(events, sizeof(CreditEvent), event_count, f5);
    fclose(f5);
    log_message(LOG_INFO, "Credit events data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save credit events data");
  }

  FILE *f6 = fopen("data_dispute_statements.bin", "wb");
  if (f6) {
    fwrite(&statement_count, sizeof(int), 1, f6);
    fwrite(&next_statement_id, sizeof(int), 1, f6);
    fwrite(statements, sizeof(DisputeStatement), statement_count, f6);
    fclose(f6);
    log_message(LOG_INFO, "Dispute statements data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save dispute statements data");
  }
}

void load_data() {
  FILE *f1 = fopen("data_orders.bin", "rb");
  if (f1) {
    fread(&order_count, sizeof(int), 1, f1);
    fread(&next_id, sizeof(int), 1, f1);
    fread(orders, sizeof(Order), order_count, f1);
    fclose(f1);
    log_message(LOG_INFO, "Loaded %d orders", order_count);
  } else {
    log_message(LOG_WARN, "No existing orders data found");
  }

  FILE *f2 = fopen("data_users.bin", "rb");
  if (f2) {
    fread(&user_count, sizeof(int), 1, f2);
    fread(users, sizeof(User), user_count, f2);
    fclose(f2);
    /* 兼容旧数据：信用分为 0 视为未初始化，回填初始分 */
    for (int i = 0; i < user_count; i++) {
      if (users[i].credit <= 0)
        users[i].credit = INITIAL_CREDIT;
    }
    log_message(LOG_INFO, "Loaded %d users", user_count);
  } else {
    log_message(LOG_WARN, "No existing users data found");
  }

  FILE *f3 = fopen("data_ratings.bin", "rb");
  if (f3) {
    fread(&rating_count, sizeof(int), 1, f3);
    fread(&next_rating_id, sizeof(int), 1, f3);
    fread(ratings, sizeof(Rating), rating_count, f3);
    fclose(f3);
    log_message(LOG_INFO, "Loaded %d ratings", rating_count);
  } else {
    log_message(LOG_WARN, "No existing ratings data found");
  }

  FILE *f4 = fopen("data_disputes.bin", "rb");
  if (f4) {
    fread(&dispute_count, sizeof(int), 1, f4);
    fread(&next_dispute_id, sizeof(int), 1, f4);
    fread(disputes, sizeof(Dispute), dispute_count, f4);
    fclose(f4);
    log_message(LOG_INFO, "Loaded %d disputes", dispute_count);
  } else {
    log_message(LOG_WARN, "No existing disputes data found");
  }

  FILE *f5 = fopen("data_events.bin", "rb");
  if (f5) {
    fread(&event_count, sizeof(int), 1, f5);
    fread(&next_event_id, sizeof(int), 1, f5);
    fread(events, sizeof(CreditEvent), event_count, f5);
    fclose(f5);
    log_message(LOG_INFO, "Loaded %d credit events", event_count);
  } else {
    log_message(LOG_WARN, "No existing credit events data found");
  }

  FILE *f6 = fopen("data_dispute_statements.bin", "rb");
  if (f6) {
    fread(&statement_count, sizeof(int), 1, f6);
    fread(&next_statement_id, sizeof(int), 1, f6);
    fread(statements, sizeof(DisputeStatement), statement_count, f6);
    fclose(f6);
    log_message(LOG_INFO, "Loaded %d dispute statements", statement_count);
  } else {
    log_message(LOG_WARN, "No existing dispute statements data found");
  }

  if (user_count == 0) {
    strcpy(users[user_count].username, "admin");
    strcpy(users[user_count].password, "123456");
    strcpy(users[user_count].real_name, "张小凡");
    strcpy(users[user_count].major, "信安 2101");
    users[user_count].credit = INITIAL_CREDIT;
    user_count++;
    save_data();
    log_message(LOG_INFO, "Created default admin user");
  }
}
