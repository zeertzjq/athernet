#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  char *port_str = strchr(argv[1], ':');
  if (port_str == NULL) {
    fprintf(stderr, "Missing port number\n");
    return EXIT_FAILURE;
  }
  int port = atoi(port_str + 1);
  if (port < 0 || port > UINT16_MAX) {
    fprintf(stderr, "Invalid port number: %d\n", port);
    return EXIT_FAILURE;
  }

  *port_str = '\0';
  char *addr = argv[1];
  struct sockaddr_in dest_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
  };
  if (inet_pton(AF_INET, addr, &dest_addr.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", addr);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }

  srand(time(NULL));
  for (int i = 0; i < 10; i++) {
    struct timespec ts = {
        .tv_sec = 1,
    };
    nanosleep(&ts, NULL);
    char payload[21];
    for (int j = 0; j < sizeof(payload) - 1; j++) {
      payload[j] = '!' + (rand() & 0x3F);
    }
    payload[sizeof(payload) - 1] = '\0';
    if (sendto(socket_fd, payload, sizeof(payload) - 1, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
      perror(NULL);
      continue;
    }
    printf("Sent Payload: %s\n", payload);
  }

  return EXIT_SUCCESS;
}
