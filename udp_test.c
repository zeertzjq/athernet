#include <arpa/inet.h>
#include <inttypes.h>
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

  if (port_str != argv[1]) {
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
    for (int i = 0; i < 10; i++) {
      struct timespec ts = {
          .tv_sec = 1,
      };
      nanosleep(&ts, NULL);
      uint32_t payload_h = time(NULL);
      uint32_t payload_n = htonl(payload_h);
      if (sendto(socket_fd, &payload_n, sizeof(payload_n), 0,
                 (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror(NULL);
        continue;
      }
      printf("Sent Payload: %u\n", payload_h);
    }
  } else {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = INADDR_ANY,
    };
    if (bind(socket_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    for (int i = 0; i < 10; i++) {
      struct sockaddr_in src_addr;
      socklen_t addrlen = sizeof(src_addr);
      uint32_t payload_n;
      if (recvfrom(socket_fd, &payload_n, sizeof(payload_n), 0,
                   (struct sockaddr *)&src_addr, &addrlen) < 0) {
        perror(NULL);
        continue;
      }
      char addr[INET_ADDRSTRLEN];
      if (inet_ntop(AF_INET, &src_addr.sin_addr, addr, sizeof(addr)) == NULL) {
        perror(NULL);
        continue;
      }
      printf("Received IP: %s, Port: %hu, Payload: %u\n", addr,
             ntohs(src_addr.sin_port), ntohl(payload_n));
    }
  }

  return EXIT_SUCCESS;
}
