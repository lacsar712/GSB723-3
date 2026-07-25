#include "credit.h"
#include "database.h"
#include "logger.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* 新分 = round(旧分 * 0.8 + 评分 * 20 * 0.2) */
int credit_new_score(int old_score, int rating_score) {
  double v = old_score * 0.8 + rating_score * 20 * 0.2;
  int result = (int)floor(v + 0.5);
  if (result < 0)
    result = 0;
  if (result > 100)
    result = 100;
  return result;
}

/* 等级映射：>=90 优 / >=75 良 / >=60 中 / <60 差 */
const char *credit_level_label(int credit) {
  if (credit >= 90)
    return "优";
  if (credit >= 75)
    return "良";
  if (credit >= 60)
    return "中";
  return "差";
}

const char *credit_level_key(int credit) {
  if (credit >= 90)
    return "excellent";
  if (credit >= 75)
    return "good";
  if (credit >= 60)
    return "fair";
  return "poor";
}

User *find_user(const char *username) {
  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0)
      return &users[i];
  }
  return NULL;
}

static Order *find_order(int order_id) {
  for (int i = 0; i < order_count; i++) {
    if (orders[i].id == order_id)
      return &orders[i];
  }
  return NULL;
}

/* 自动任务：
 * 1) 待裁决纠纷超过 24h 未有新说明 -> 按接单方责任自动成立并退款标记；
 * 2) 已确认送达 (delivered) 超过 2h 未确认完成也未发起纠纷 -> 自动完成订单。 */
void run_auto_tasks() {
  long now = (long)time(NULL);
  int changed = 0;

  for (int i = 0; i < dispute_count; i++) {
    Dispute *d = &disputes[i];
    if (strcmp(d->status, "pending") != 0)
      continue;
    if (now - d->created_at >= DISPUTE_AUTO_UPHOLD_SECONDS) {
      strcpy(d->status, "upheld");
      strcpy(d->verdict,
             "超过 24 小时双方未补充说明，系统按「接单方责任」自动成立，"
             "悬赏金额已标记退回。");
      d->resolved_at = now;
      Order *o = find_order(d->order_id);
      if (o) {
        o->frozen = 0;
        strcpy(o->status, "cancelled"); /* 成立=已撤回类终态并退市 */
      }
      changed = 1;
      log_message(LOG_INFO, "Dispute %d auto-upheld (24h)", d->id);
    }
  }

  for (int i = 0; i < order_count; i++) {
    Order *o = &orders[i];
    if (o->frozen)
      continue;
    if (strcmp(o->status, "delivered") != 0)
      continue;
    if (o->delivered_at > 0 &&
        now - o->delivered_at >= DELIVERED_AUTO_COMPLETE_SECONDS) {
      strcpy(o->status, "completed");
      changed = 1;
      log_message(LOG_INFO, "Order %d auto-completed (2h after delivered)",
                  o->id);
    }
  }

  if (changed)
    save_data();
}
