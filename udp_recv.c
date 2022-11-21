#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

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
  int bind_port = atoi(port_str + 1);
  if (bind_port < 0 || bind_port > UINT16_MAX) {
    fprintf(stderr, "Invalid port number: %d\n", bind_port);
    return EXIT_FAILURE;
  }
  *port_str = '\0';

  char *addr = argv[1];
  struct sockaddr_in bind_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(bind_port),
  };
  if (inet_pton(AF_INET, addr, &bind_addr.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", addr);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }
  if (bind(socket_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
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
           addr, ntohs(src_addr.sin_port), ntohs(bind_addr.sin_port), payload);
  }
}
