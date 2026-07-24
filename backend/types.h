#ifndef TYPES_H
#define TYPES_H

#define MAX_ORDERS 500
#define MAX_USERS 100
#define MAX_RATINGS 1000
#define MAX_DISPUTES 500
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
  char result[220];
  char creator_resp[130];
  char worker_resp[130];
  long created_at;
  long resolved_at;
  char prev_status[20];
} Dispute;

#endif
