#include "database.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
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

User* find_user(const char *username) {
  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0)
      return &users[i];
  }
  return NULL;
}

Order* find_order(int id) {
  for (int i = 0; i < order_count; i++) {
    if (orders[i].id == id)
      return &orders[i];
  }
  return NULL;
}

int compute_credit_level(int score) {
  if (score >= 90) return 0;
  if (score >= 75) return 1;
  if (score >= 60) return 2;
  return 3;
}

const char* credit_level_label(int level) {
  switch (level) {
    case 0: return "优";
    case 1: return "良";
    case 2: return "中";
    case 3: return "差";
    default: return "中";
  }
}

void update_credit_score(const char *username, int new_score) {
  User *u = find_user(username);
  if (u) {
    if (new_score < 0) new_score = 0;
    if (new_score > 100) new_score = 100;
    u->credit_score = new_score;
  }
}

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
    int stored_count = 0;
    if (fread(&stored_count, sizeof(int), 1, f2) == 1 && stored_count > 0 && stored_count < MAX_USERS) {
      user_count = stored_count;
      fseek(f2, 0, SEEK_END);
      long file_size = ftell(f2);
      fseek(f2, sizeof(int), SEEK_SET);
      long body_size = file_size - (long)sizeof(int);
      int record_size = (int)(body_size / user_count);
      int use_new_size = (record_size == (int)sizeof(User));
      for (int i = 0; i < user_count; i++) {
        memset(&users[i], 0, sizeof(User));
        if (use_new_size) {
          if (fread(&users[i], sizeof(User), 1, f2) != 1) break;
        } else {
          char buf[200];
          if (fread(buf, 200, 1, f2) != 1) break;
          memcpy(users[i].username, buf, 50);
          memcpy(users[i].password, buf + 50, 50);
          memcpy(users[i].real_name, buf + 100, 50);
          memcpy(users[i].major, buf + 150, 50);
          users[i].credit_score = 100;
        }
        if (users[i].credit_score <= 0) users[i].credit_score = 100;
      }
    }
    fclose(f2);
    log_message(LOG_INFO, "Loaded %d users", user_count);
  } else {
    log_message(LOG_WARN, "No existing users data found");
  }

  if (user_count == 0) {
    strcpy(users[user_count].username, "admin");
    strcpy(users[user_count].password, "123456");
    strcpy(users[user_count].real_name, "张小凡");
    strcpy(users[user_count].major, "信安 2101");
    users[user_count].credit_score = 100;
    user_count++;
    save_data();
    log_message(LOG_INFO, "Created default admin user");
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
}
