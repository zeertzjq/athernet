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
  int dest_port = atoi(port_str + 1);
  if (dest_port < 0 || dest_port > UINT16_MAX) {
    fprintf(stderr, "Invalid port number: %d\n", dest_port);
    return EXIT_FAILURE;
  }
  *port_str = '\0';

  char *addr = argv[1];
  struct sockaddr_in dest_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(dest_port),
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

  char payload[50];
  while (fgets(payload, 50, stdin) != NULL) {
    size_t payload_len = strlen(payload);
    if (payload[payload_len - 1] == '\n') {
      payload[--payload_len] = '\0';
    }
    if (sendto(socket_fd, payload, payload_len, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
      perror(NULL);
      continue;
    }
    printf("Sent Payload: %s\n", payload);
    struct timespec ts = {
        .tv_nsec = 500000000,
    };
    nanosleep(&ts, NULL);
  }

  return EXIT_SUCCESS;
}
