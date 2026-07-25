#include "database.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

void add_event(const char *username, const char *event_type, const char *ref_type,
               int ref_id, int old_score, int new_score, const char *detail,
               const char *actor) {
  if (event_count >= MAX_EVENTS) return;
  CreditEvent *e = &events[event_count++];
  memset(e, 0, sizeof(CreditEvent));
  e->id = next_event_id++;
  strncpy(e->username, username, sizeof(e->username) - 1);
  strncpy(e->event_type, event_type, sizeof(e->event_type) - 1);
  strncpy(e->ref_type, ref_type, sizeof(e->ref_type) - 1);
  e->ref_id = ref_id;
  e->old_score = old_score;
  e->new_score = new_score;
  if (detail) strncpy(e->detail, detail, sizeof(e->detail) - 1);
  if (actor) strncpy(e->actor, actor, sizeof(e->actor) - 1);
  e->created_at = (long long)time(NULL);
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

  FILE *f5 = fopen("data_events.bin", "wb");
  if (f5) {
    fwrite(&event_count, sizeof(int), 1, f5);
    fwrite(&next_event_id, sizeof(int), 1, f5);
    fwrite(events, sizeof(CreditEvent), event_count, f5);
    fclose(f5);
    log_message(LOG_INFO, "Events data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save events data");
  }
}

void load_data() {
  FILE *f1 = fopen("data_orders.bin", "rb");
  if (f1) {
    int stored_count = 0;
    if (fread(&stored_count, sizeof(int), 1, f1) == 1 && stored_count > 0 && stored_count < MAX_ORDERS) {
      order_count = stored_count;
      fread(&next_id, sizeof(int), 1, f1);
      fseek(f1, 0, SEEK_END);
      long file_size = ftell(f1);
      fseek(f1, 2 * sizeof(int), SEEK_SET);
      long body_size = file_size - 2 * (long)sizeof(int);
      int record_size = (int)(body_size / order_count);
      int new_size = (int)sizeof(Order);
      for (int i = 0; i < order_count; i++) {
        memset(&orders[i], 0, sizeof(Order));
        if (record_size == new_size) {
          if (fread(&orders[i], new_size, 1, f1) != 1) break;
        } else {
          char buf[600];
          int rd = record_size > 600 ? 600 : record_size;
          if (fread(buf, rd, 1, f1) != 1) break;
          int off = 0;
          memcpy(&orders[i].id, buf + off, sizeof(int)); off += 4;
          memcpy(orders[i].creator, buf + off, 50); off += 50;
          memcpy(orders[i].worker, buf + off, 50); off += 50;
          if (off + 100 <= rd) memcpy(orders[i].package_info, buf + off, 100); off += 100;
          if (off + 100 <= rd) memcpy(orders[i].pickup_addr, buf + off, 100); off += 100;
          if (off + 100 <= rd) memcpy(orders[i].delivery_addr, buf + off, 100); off += 100;
          if (off + 20 <= rd) memcpy(orders[i].reward, buf + off, 20); off += 20;
          if (off + 50 <= rd) memcpy(orders[i].category, buf + off, 50); off += 50;
          if (off + 20 <= rd) memcpy(orders[i].status, buf + off, 20); off += 20;
          if (off + 8 <= rd) memcpy(&orders[i].created_at, buf + off, 8); off += 8;
          if (off + 8 <= rd) memcpy(&orders[i].accepted_at, buf + off, 8); off += 8;
          if (off + 8 <= rd) memcpy(&orders[i].delivered_at, buf + off, 8); off += 8;
          if (off + 8 <= rd) memcpy(&orders[i].completed_at, buf + off, 8); off += 8;
          if (off + 4 <= rd) memcpy(&orders[i].frozen, buf + off, 4); off += 4;
          if (off + 20 <= rd) memcpy(orders[i].prev_status, buf + off, 20); off += 20;
          if (off + 4 <= rd) memcpy(&orders[i].creator_rated, buf + off, 4); off += 4;
          if (off + 4 <= rd) memcpy(&orders[i].worker_rated, buf + off, 4); off += 4;
          if (off + 4 <= rd) memcpy(&orders[i].disputed, buf + off, 4);
        }
      }
    }
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
    int stored_count = 0;
    if (fread(&stored_count, sizeof(int), 1, f4) == 1 && stored_count > 0 && stored_count < MAX_DISPUTES) {
      dispute_count = stored_count;
      fread(&next_dispute_id, sizeof(int), 1, f4);
      fseek(f4, 0, SEEK_END);
      long file_size = ftell(f4);
      fseek(f4, 2 * sizeof(int), SEEK_SET);
      long body_size = file_size - 2 * (long)sizeof(int);
      int record_size = (int)(body_size / dispute_count);
      int new_size = (int)sizeof(Dispute);
      for (int i = 0; i < dispute_count; i++) {
        memset(&disputes[i], 0, sizeof(Dispute));
        if (record_size == new_size) {
          if (fread(&disputes[i], new_size, 1, f4) != 1) break;
        } else {
          char buf[600];
          if (fread(buf, record_size > 600 ? 600 : record_size, 1, f4) != 1) break;
          memcpy(&disputes[i], buf, record_size > 600 ? 600 : record_size);
        }
      }
    }
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
    log_message(LOG_INFO, "Loaded %d events", event_count);
  } else {
    log_message(LOG_WARN, "No existing events data found");
  }
}
