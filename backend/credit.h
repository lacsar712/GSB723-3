#ifndef CREDIT_H
#define CREDIT_H

#include "types.h"

/* 信用分核心规则（写死） */
int credit_new_score(int old_score, int rating_score);
const char *credit_level_label(int credit); /* 优 / 良 / 中 / 差 */
const char *credit_level_key(int credit);   /* excellent / good / fair / poor */

/* 用户查找 */
User *find_user(const char *username);

/* 自动裁决 / 自动完成的时间驱动检查（每次请求进入时调用一次） */
void run_auto_tasks();

#endif
