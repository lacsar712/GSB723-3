#include "database.h"
#include "logger.h"
#include "routes.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 2082


int main() {
  load_data();
  log_message(LOG_INFO, "=== Campus Express Delivery System Starting ===");

  int server_fd, client_socket;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    log_message(LOG_ERROR, "Socket creation failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    log_message(LOG_ERROR, "Setsockopt failed");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    log_message(LOG_ERROR, "Bind failed on port %d", PORT);
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 10) < 0) {
    log_message(LOG_ERROR, "Listen failed");
    exit(EXIT_FAILURE);
  }

  log_message(LOG_INFO, "Server listening on port %d", PORT);

  while (1) {
    if ((client_socket = accept(server_fd, (struct sockaddr *)&address,
                                (socklen_t *)&addrlen)) < 0) {
      log_message(LOG_WARN, "Accept failed, continuing...");
      continue;
    }
    handle_request(client_socket);
  }

  return 0;
}
