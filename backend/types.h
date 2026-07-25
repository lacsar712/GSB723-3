#ifndef TYPES_H
#define TYPES_H

#define MAX_ORDERS 500
#define MAX_USERS 100
#define MAX_RATINGS 1000
#define MAX_DISPUTES 500
#define MAX_EVENTS 2000
#define BUFFER_SIZE 20480

#define INITIAL_CREDIT 100
#define MIN_ACCEPT_CREDIT 60

typedef struct {
  char username[50];
  char password[50];
  char real_name[50];
  char major[50];
  int credit_score;
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
  long created_at;
  long accepted_at;
  long delivered_at;
  int frozen;
} Order;

typedef struct {
  int id;
  int order_id;
  char from_user[50];
  char to_user[50];
  int score;
  char comment[60];
  long created_at;
} Rating;

typedef struct {
  int id;
  int order_id;
  char initiator[50];
  char reason[110];
  char status[16];
  char result[320];
  char creator_resp[700];
  char creator_evidence_type[24];
  char creator_evidence_desc[200];
  long creator_resp_at;
  char worker_resp[700];
  char worker_evidence_type[24];
  char worker_evidence_desc[200];
  long worker_resp_at;
  long created_at;
  long resolved_at;
  char prev_status[20];
} Dispute;

typedef struct {
  int id;
  char user[50];
  char type[24];
  int order_id;
  int dispute_id;
  int rating_id;
  char actor[50];
  int score_before;
  int score_after;
  int rating_score;
  char detail[220];
  long created_at;
} CreditEvent;

#endif
