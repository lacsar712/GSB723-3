#ifndef TYPES_H
#define TYPES_H

#define MAX_ORDERS 500
#define MAX_USERS 100
#define MAX_RATINGS 2000
#define MAX_DISPUTES 500
#define BUFFER_SIZE 20480

/* 信用分业务常量（写死，不做可配置开关） */
#define INITIAL_CREDIT 100
#define MIN_ACCEPT_CREDIT 60
#define DISPUTE_AUTO_UPHOLD_SECONDS (24 * 3600) /* 24 小时自动按接单方责任成立 */
#define DELIVERED_AUTO_COMPLETE_SECONDS (2 * 3600) /* 已确认送达 2 小时自动完成 */

typedef struct {
  char username[50];
  char password[50];
  char real_name[50];
  char major[50];
  int credit; /* 信用分，初始 100 */
} User;

typedef struct {
  int id;
  char creator[50];
  char worker[50];
  char package_info[100];
  char pickup_addr[100];
  char delivery_addr[100];
  char reward[20];
  char category[50];
  char status[20];
  int creator_rated;      /* 发布方是否已评价接单方 */
  int worker_rated;       /* 接单方是否已评价发布方 */
  int frozen;             /* 是否因纠纷冻结，冻结后不可普通流转 */
  char prev_status[20];   /* 冻结前状态，纠纷驳回后恢复 */
  long delivered_at;      /* 进入 delivered 的时间戳，用于 2 小时自动完成 */
} Order;

typedef struct {
  int id;
  int order_id;
  char rater[50];    /* 评分人 */
  char ratee[50];    /* 被评分人 */
  int score;         /* 1-5 星 */
  char comment[160]; /* 评语，最长 50 字 */
  long created_at;
} Rating;

typedef struct {
  int id;
  int order_id;
  char initiator[50];  /* 发起方 */
  char reason[320];    /* 原因，最长 100 字 */
  char status[20];     /* pending / upheld / rejected */
  char verdict[320];   /* 裁决说明 */
  long created_at;
  long resolved_at;
} Dispute;

#endif
