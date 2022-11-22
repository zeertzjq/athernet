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

enum {
  NODE_UNKNOWN,
  NODE_SEND,
  NODE_RECV,
} node_type = NODE_UNKNOWN;

int main(int argc, char **argv) {
  if (argc <= 2) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "send") == 0) {
    node_type = NODE_SEND;
  } else if (strcmp(argv[1], "recv") == 0) {
    node_type = NODE_RECV;
  } else {
    fprintf(stderr, "Invalid argument: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  char *addr_str = argv[2];
  char *port_str = strchr(addr_str, ':');
  if (port_str == NULL) {
    fprintf(stderr, "Missing port number\n");
    return EXIT_FAILURE;
  }
  int port_arg = atoi(port_str + 1);
  if (port_arg < 0 || port_arg > UINT16_MAX) {
    fprintf(stderr, "Invalid port number: %d\n", port_arg);
    return EXIT_FAILURE;
  }
  *port_str = '\0';

  struct sockaddr_in addr_arg = {
      .sin_family = AF_INET,
      .sin_port = htons(port_arg),
  };
  if (inet_pton(AF_INET, addr_str, &addr_arg.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", addr_str);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }

  if (node_type == NODE_SEND) {
    char payload[50];
    while (fgets(payload, 50, stdin) != NULL) {
      size_t payload_len = strlen(payload);
      if (payload[payload_len - 1] == '\n') {
        payload[--payload_len] = '\0';
      }
      if (sendto(socket_fd, payload, payload_len, 0,
                 (struct sockaddr *)&addr_arg, sizeof(addr_arg)) < 0) {
        perror(NULL);
        continue;
      }
      printf("Sent Payload: %s\n", payload);
      struct timespec ts = {
          .tv_nsec = 500000000,
      };
      nanosleep(&ts, NULL);
    }
  } else if (node_type == NODE_RECV) {
    if (bind(socket_fd, (struct sockaddr *)&addr_arg, sizeof(addr_arg)) < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    for (;;) {
      struct sockaddr_in src_addr;
      socklen_t addrlen = sizeof(src_addr);
      char payload[50];
      if (recvfrom(socket_fd, payload, sizeof(payload) - 1, 0,
                   (struct sockaddr *)&src_addr, &addrlen) < 0) {
        perror(NULL);
        continue;
      }
      char addr[INET_ADDRSTRLEN];
      if (inet_ntop(AF_INET, &src_addr.sin_addr, addr, sizeof(addr)) == NULL) {
        perror(NULL);
        continue;
      }
      printf("Received IP: %s, Source Port: %hu, Dest Port: %hu, Payload: %s\n",
             addr, ntohs(src_addr.sin_port), ntohs(addr_arg.sin_port), payload);
    }
  }

  return EXIT_SUCCESS;
}
