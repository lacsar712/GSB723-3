#ifndef TYPES_H
#define TYPES_H

#define MAX_ORDERS 500
#define MAX_USERS 100
#define BUFFER_SIZE 20480

typedef struct {
  char username[50];
  char password[50];
  char real_name[50];
  char major[50];
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
} Order;

#endif
