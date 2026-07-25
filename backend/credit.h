#ifndef CREDIT_H
#define CREDIT_H

#include "types.h"

/* 信用分核心规则（写死） */
int credit_new_score(int old_score, int rating_score);
const char *credit_level_label(int credit); /* 优 / 良 / 中 / 差 */
const char *credit_level_key(int credit);   /* excellent / good / fair / poor */

/* 用户查找 */
User *find_user(const char *username);

/* 追加一条信用事件到时间线。
 * score / old_credit / new_credit 不适用时传 0 / -1 / -1。
 * 不在此函数内 save_data，由调用方统一持久化。 */
void add_credit_event(const char *owner, const char *type, const char *ref_type,
                      int ref_id, int score, int old_credit, int new_credit,
                      const char *detail);

/* 自动裁决 / 自动完成的时间驱动检查（每次请求进入时调用一次） */
void run_auto_tasks();

#endif
