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

  char *addr_str = argv[1];
  char *port_str = strchr(addr_str, ':');
  int filter_port = -1;
  if (port_str != NULL) {
    filter_port = atoi(port_str + 1);
    if (filter_port < 0 || filter_port > UINT16_MAX) {
      fprintf(stderr, "Invalid port number: %d\n", filter_port);
      return EXIT_FAILURE;
    }
    *port_str = '\0';
  }

  struct sockaddr_in bind_addr = {
      .sin_family = AF_INET,
      .sin_port = 0,
  };
  if (inet_pton(AF_INET, addr_str, &bind_addr.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", addr_str);
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
    char raw_payload[sizeof(struct iphdr) + sizeof(struct udphdr) + 50];
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
    if (filter_port >= 0 && ntohs(udp_hdr_p->uh_dport) != filter_port) {
      continue;
    }
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    printf("Received IP: %s, Source Port: %hu, Dest Port: %hu, Payload: %s\n",
           addr, ntohs(udp_hdr_p->uh_sport), ntohs(udp_hdr_p->uh_dport),
           udp_payload);
  }
}
