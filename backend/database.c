#include "database.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

User users[MAX_USERS];
int user_count = 0;
Order orders[MAX_ORDERS];
int order_count = 0;
int next_id = 1;

void save_data() {
  FILE *f1 = fopen("data_orders.bin", "wb");
  if (f1) {
    fwrite(&order_count, sizeof(int), 1, f1);
    fwrite(&next_id, sizeof(int), 1, f1);
    fwrite(orders, sizeof(Order), order_count, f1);
    fclose(f1);
    log_message(LOG_INFO, "Orders data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save orders data");
  }

  FILE *f2 = fopen("data_users.bin", "wb");
  if (f2) {
    fwrite(&user_count, sizeof(int), 1, f2);
    fwrite(users, sizeof(User), user_count, f2);
    fclose(f2);
    log_message(LOG_INFO, "Users data saved successfully");
  } else {
    log_message(LOG_ERROR, "Failed to save users data");
  }
}

void load_data() {
  FILE *f1 = fopen("data_orders.bin", "rb");
  if (f1) {
    fread(&order_count, sizeof(int), 1, f1);
    fread(&next_id, sizeof(int), 1, f1);
    fread(orders, sizeof(Order), order_count, f1);
    fclose(f1);
    log_message(LOG_INFO, "Loaded %d orders", order_count);
  } else {
    log_message(LOG_WARN, "No existing orders data found");
  }

  FILE *f2 = fopen("data_users.bin", "rb");
  if (f2) {
    fread(&user_count, sizeof(int), 1, f2);
    fread(users, sizeof(User), user_count, f2);
    fclose(f2);
    log_message(LOG_INFO, "Loaded %d users", user_count);
  } else {
    log_message(LOG_WARN, "No existing users data found");
  }

  if (user_count == 0) {
    strcpy(users[user_count].username, "admin");
    strcpy(users[user_count].password, "123456");
    strcpy(users[user_count].real_name, "张小凡");
    strcpy(users[user_count].major, "信安 2101");
    user_count++;
    save_data();
    log_message(LOG_INFO, "Created default admin user");
  }
}
