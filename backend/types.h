#ifndef TYPES_H
#define TYPES_H

#define MAX_ORDERS 500
#define MAX_USERS 100
#define MAX_RATINGS 2000
#define MAX_DISPUTES 500
#define MAX_EVENTS 8000
#define MAX_STATEMENTS 1000
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

/* 纠纷补充说明：待裁决期间，发布方 / 接单方各可提交一次。
 * role: creator / worker
 * evidence_type: screenshot（截图说明）/ chatlog（聊天记录）/ other（其他）/ 空 */
typedef struct {
  int id;
  int dispute_id;
  char role[12];          /* creator / worker */
  char author[50];        /* 提交人用户名 */
  char content[640];      /* 补充说明正文，最长 200 字 */
  char evidence_type[16]; /* 证据类型，可选 */
  char evidence_desc[160];/* 证据一句话说明，最长 50 字，可选 */
  long created_at;
} DisputeStatement;

/* 信用事件时间线
 * type: rating_received / rating_given / dispute_created /
 *       dispute_upheld / dispute_rejected / credit_change
 * ref_type: order / dispute （供前端跳转关联详情） */
typedef struct {
  int id;
  char owner[50];     /* 事件归属用户 */
  char type[24];      /* 事件类型 */
  char ref_type[12];  /* order / dispute */
  int ref_id;         /* 关联订单号或纠纷号 */
  int score;          /* 评价类事件的星级，其它为 0 */
  int old_credit;     /* 信用分变更前分值，非变更事件为 -1 */
  int new_credit;     /* 信用分变更后分值，非变更事件为 -1 */
  char detail[160];   /* 附加说明/评语 */
  long created_at;
} CreditEvent;

#endif
