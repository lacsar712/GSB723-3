#ifndef TYPES_H
#define TYPES_H

#define MAX_ORDERS 500
#define MAX_USERS 100
#define MAX_RATINGS 1000
#define MAX_DISPUTES 200
#define BUFFER_SIZE 20480

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
  long long created_at;
  long long accepted_at;
  long long delivered_at;
  long long completed_at;
  int frozen;
  char prev_status[20];
  int creator_rated;
  int worker_rated;
  int disputed;
} Order;

typedef struct {
  int id;
  int order_id;
  char rater[50];
  char ratee[50];
  int score;
  char comment[100];
  long long created_at;
} Rating;

typedef struct {
  int id;
  int order_id;
  char initiator[50];
  char reason[200];
  char status[20];
  long long created_at;
  long long resolved_at;
  char resolution[200];
  char prev_order_status[20];
} Dispute;

#endif
