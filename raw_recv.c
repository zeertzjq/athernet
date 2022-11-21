#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
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

  struct sockaddr_in bind_addr = {
      .sin_family = AF_INET,
      .sin_port = 0,
  };
  if (inet_pton(AF_INET, argv[1], &bind_addr.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
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
    char raw_payload[sizeof(struct iphdr) + sizeof(struct udphdr) + 40];
    memset(raw_payload, 0, sizeof(raw_payload));
    if (recvfrom(socket_fd, &raw_payload, sizeof(raw_payload) - 1, 0,
                 (struct sockaddr *)&src_addr, &addrlen) < 0) {
      perror(NULL);
      continue;
    }
    char addr[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &src_addr.sin_addr, addr, sizeof(addr)) == NULL) {
      perror(NULL);
      continue;
    }
    char *ip_payload = raw_payload + sizeof(struct iphdr);
    struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    printf("Received IP: %s, Source Port: %hu, Dest Port: %hu, Payload: %s\n",
           addr, ntohs(udp_hdr_p->source), ntohs(udp_hdr_p->dest), udp_payload);
  }
}
