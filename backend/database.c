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

User *find_user(const char *username) {
  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0)
      return &users[i];
  }
  return NULL;
}

Order *find_order(int id) {
  for (int i = 0; i < order_count; i++) {
    if (orders[i].id == id)
      return &orders[i];
  }
  return NULL;
}

int credit_of(const char *username) {
  User *u = find_user(username);
  return u ? u->credit_score : INITIAL_CREDIT;
}

const char *credit_grade(int score) {
  if (score >= 90) return "优";
  if (score >= 75) return "良";
  if (score >= 60) return "中";
  return "差";
}

int can_accept_orders(const char *username) {
  return credit_of(username) >= MIN_ACCEPT_CREDIT;
}

void apply_rating_credit(Rating *r) {
  User *u = find_user(r->to_user);
  if (!u) return;
  double next = u->credit_score * 0.8 + r->score * 20.0 * 0.2;
  int rounded = (int)(next + 0.5);
  if (rounded < 0) rounded = 0;
  if (rounded > 100) rounded = 100;
  u->credit_score = rounded;
  log_message(LOG_INFO, "Credit updated for %s: %d (from rating %d by %s)",
              u->username, rounded, r->score, r->from_user);
}

int has_rating(int order_id, const char *from_user) {
  for (int i = 0; i < rating_count; i++) {
    if (ratings[i].order_id == order_id &&
        strcmp(ratings[i].from_user, from_user) == 0)
      return 1;
  }
  return 0;
}

Dispute *find_dispute_by_order(int order_id) {
  for (int i = 0; i < dispute_count; i++) {
    if (disputes[i].order_id == order_id)
      return &disputes[i];
  }
  return NULL;
}

Dispute *find_dispute(int id) {
  for (int i = 0; i < dispute_count; i++) {
    if (disputes[i].id == id)
      return &disputes[i];
  }
  return NULL;
}

void add_event(const char *user, const char *type, int order_id, int dispute_id,
               int rating_id, const char *actor, int before, int after,
               int rating_score, const char *detail) {
  if (!user || strlen(user) == 0) return;
  if (event_count >= MAX_EVENTS) return;
  CreditEvent *e = &events[event_count++];
  memset(e, 0, sizeof(CreditEvent));
  e->id = next_event_id++;
  strncpy(e->user, user, sizeof(e->user) - 1);
  strncpy(e->type, type, sizeof(e->type) - 1);
  e->order_id = order_id;
  e->dispute_id = dispute_id;
  e->rating_id = rating_id;
  if (actor) strncpy(e->actor, actor, sizeof(e->actor) - 1);
  e->score_before = before;
  e->score_after = after;
  e->rating_score = rating_score;
  if (detail) strncpy(e->detail, detail, sizeof(e->detail) - 1);
  e->created_at = (long)time(NULL);
}

void rule_dispute(Dispute *d, int upheld, const char *result_text) {
  if (strcmp(d->status, "pending") != 0) return;
  Order *o = find_order(d->order_id);
  d->resolved_at = (long)time(NULL);
  const char *evt_type = upheld ? "dispute_upheld" : "dispute_rejected";
  char detail[260];
  snprintf(detail, sizeof(detail), "%s", result_text ? result_text : "");
  if (upheld) {
    strcpy(d->status, "upheld");
    snprintf(d->result, sizeof(d->result), "%s", result_text);
    if (o) {
      strcpy(o->status, "refunded");
      o->frozen = 0;
    }
    log_message(LOG_WARN, "Dispute %d upheld for order %d", d->id, d->order_id);
  } else {
    strcpy(d->status, "rejected");
    snprintf(d->result, sizeof(d->result), "%s", result_text);
    if (o) {
      if (strlen(d->prev_status) > 0)
        strcpy(o->status, d->prev_status);
      o->frozen = 0;
    }
    log_message(LOG_INFO, "Dispute %d rejected for order %d", d->id, d->order_id);
  }
  if (o) {
    add_event(o->creator, evt_type, o->id, d->id, 0, "system", 0, 0, 0, detail);
    if (strlen(o->worker) > 0)
      add_event(o->worker, evt_type, o->id, d->id, 0, "system", 0, 0, 0, detail);
  } else {
    add_event(d->initiator, evt_type, d->order_id, d->id, 0, "system", 0, 0, 0, detail);
  }
}

void run_auto_settlement() {
  long now = (long)time(NULL);
  int changed = 0;

  for (int i = 0; i < order_count; i++) {
    Order *o = &orders[i];
    if (strcmp(o->status, "delivered") == 0 && o->delivered_at > 0 &&
        o->frozen == 0) {
      if (now - o->delivered_at >= 2 * 3600) {
        Dispute *d = find_dispute_by_order(o->id);
        if (!d || strcmp(d->status, "pending") != 0) {
          strcpy(o->status, "completed");
          log_message(LOG_INFO,
                      "Auto-completed order %d (delivered >2h, no dispute)",
                      o->id);
          changed = 1;
        }
      }
    }
  }

  for (int i = 0; i < dispute_count; i++) {
    Dispute *d = &disputes[i];
    if (strcmp(d->status, "pending") == 0 && d->created_at > 0) {
      if (now - d->created_at >= 24 * 3600) {
        int no_response = (strlen(d->creator_resp) == 0 && strlen(d->worker_resp) == 0);
        if (no_response) {
          char txt[256];
          snprintf(txt, sizeof(txt),
                   "系统自动裁决：发起纠纷 24 小时内双方均未补充说明，"
                   "判定为接单方责任，悬赏金额 %s 退回发布方。",
                   find_order(d->order_id) ? find_order(d->order_id)->reward : "");
          rule_dispute(d, 1, txt);
          changed = 1;
        }
      }
    }
  }

  if (changed) save_data();
}

void save_data() {
  FILE *f1 = fopen("data_orders.bin", "wb");
  if (f1) {
    fwrite(&order_count, sizeof(int), 1, f1);
    fwrite(&next_id, sizeof(int), 1, f1);
    fwrite(orders, sizeof(Order), order_count, f1);
    fclose(f1);
  } else {
    log_message(LOG_ERROR, "Failed to save orders data");
  }

  FILE *f2 = fopen("data_users.bin", "wb");
  if (f2) {
    fwrite(&user_count, sizeof(int), 1, f2);
    fwrite(users, sizeof(User), user_count, f2);
    fclose(f2);
  } else {
    log_message(LOG_ERROR, "Failed to save users data");
  }

  FILE *f3 = fopen("data_ratings.bin", "wb");
  if (f3) {
    fwrite(&rating_count, sizeof(int), 1, f3);
    fwrite(&next_rating_id, sizeof(int), 1, f3);
    fwrite(ratings, sizeof(Rating), rating_count, f3);
    fclose(f3);
  } else {
    log_message(LOG_ERROR, "Failed to save ratings data");
  }

  FILE *f4 = fopen("data_disputes.bin", "wb");
  if (f4) {
    fwrite(&dispute_count, sizeof(int), 1, f4);
    fwrite(&next_dispute_id, sizeof(int), 1, f4);
    fwrite(disputes, sizeof(Dispute), dispute_count, f4);
    fclose(f4);
  } else {
    log_message(LOG_ERROR, "Failed to save disputes data");
  }

  FILE *f5 = fopen("data_events.bin", "wb");
  if (f5) {
    fwrite(&event_count, sizeof(int), 1, f5);
    fwrite(&next_event_id, sizeof(int), 1, f5);
    fwrite(events, sizeof(CreditEvent), event_count, f5);
    fclose(f5);
  } else {
    log_message(LOG_ERROR, "Failed to save events data");
  }

  log_message(LOG_INFO,
              "Data saved (orders=%d users=%d ratings=%d disputes=%d events=%d)",
              order_count, user_count, rating_count, dispute_count, event_count);
}

void load_data() {
  FILE *f1 = fopen("data_orders.bin", "rb");
  if (f1) {
    int cnt = 0;
    if (fread(&cnt, sizeof(int), 1, f1) == 1) {
      if (cnt <= MAX_ORDERS) order_count = cnt;
      fread(&next_id, sizeof(int), 1, f1);
      if (order_count > 0)
        fread(orders, sizeof(Order), order_count, f1);
    }
    fclose(f1);
    log_message(LOG_INFO, "Loaded %d orders", order_count);
  } else {
    log_message(LOG_WARN, "No existing orders data found");
  }

  FILE *f2 = fopen("data_users.bin", "rb");
  if (f2) {
    int cnt = 0;
    if (fread(&cnt, sizeof(int), 1, f2) == 1) {
      if (cnt <= MAX_USERS) user_count = cnt;
      if (user_count > 0)
        fread(users, sizeof(User), user_count, f2);
    }
    fclose(f2);
    log_message(LOG_INFO, "Loaded %d users", user_count);
  } else {
    log_message(LOG_WARN, "No existing users data found");
  }

  FILE *f3 = fopen("data_ratings.bin", "rb");
  if (f3) {
    int cnt = 0;
    if (fread(&cnt, sizeof(int), 1, f3) == 1) {
      if (cnt <= MAX_RATINGS) rating_count = cnt;
      fread(&next_rating_id, sizeof(int), 1, f3);
      if (rating_count > 0)
        fread(ratings, sizeof(Rating), rating_count, f3);
    }
    fclose(f3);
    log_message(LOG_INFO, "Loaded %d ratings", rating_count);
  } else {
    log_message(LOG_WARN, "No existing ratings data found");
  }

  FILE *f4 = fopen("data_disputes.bin", "rb");
  if (f4) {
    int cnt = 0;
    if (fread(&cnt, sizeof(int), 1, f4) == 1) {
      if (cnt <= MAX_DISPUTES) dispute_count = cnt;
      fread(&next_dispute_id, sizeof(int), 1, f4);
      if (dispute_count > 0)
        fread(disputes, sizeof(Dispute), dispute_count, f4);
    }
    fclose(f4);
    log_message(LOG_INFO, "Loaded %d disputes", dispute_count);
  } else {
    log_message(LOG_WARN, "No existing disputes data found");
  }

  FILE *f5 = fopen("data_events.bin", "rb");
  if (f5) {
    int cnt = 0;
    if (fread(&cnt, sizeof(int), 1, f5) == 1) {
      if (cnt <= MAX_EVENTS) event_count = cnt;
      fread(&next_event_id, sizeof(int), 1, f5);
      if (event_count > 0)
        fread(events, sizeof(CreditEvent), event_count, f5);
    }
    fclose(f5);
    log_message(LOG_INFO, "Loaded %d events", event_count);
  } else {
    log_message(LOG_WARN, "No existing events data found");
  }

  for (int i = 0; i < user_count; i++) {
    if (users[i].credit_score <= 0) users[i].credit_score = INITIAL_CREDIT;
  }

  if (user_count == 0) {
    strcpy(users[user_count].username, "admin");
    strcpy(users[user_count].password, "123456");
    strcpy(users[user_count].real_name, "张小凡");
    strcpy(users[user_count].major, "信安 2101");
    users[user_count].credit_score = INITIAL_CREDIT;
    user_count++;
    save_data();
    log_message(LOG_INFO, "Created default admin user");
  }
}
