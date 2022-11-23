#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
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
  struct sockaddr_in saddr_bind = {
      .sin_family = AF_INET,
      .sin_port = 0,
  };
  if (inet_pton(AF_INET, addr_str, &saddr_bind.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", addr_str);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (socket_fd < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }
  if (bind(socket_fd, (struct sockaddr *)&saddr_bind, sizeof(saddr_bind)) < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }

  for (;;) {
    char raw_payload[sizeof(struct iphdr) + sizeof(struct icmphdr) + 50] = {0};
    if (recvfrom(socket_fd, raw_payload, sizeof(raw_payload) - 1, 0, NULL,
                 NULL) < 0) {
      perror(NULL);
      continue;
    }
    struct iphdr *ip_hdr_p = (struct iphdr *)raw_payload;
    struct in_addr addr_src = {
        .s_addr = ip_hdr_p->saddr,
    };
    char addr[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &addr_src, addr, sizeof(addr)) == NULL) {
      perror(NULL);
      continue;
    }
    char *ip_payload = raw_payload + sizeof(struct iphdr);
    struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
    printf("Received IP: %s, Type: %hhu, Code: %hhu, ID: %hu, Seq: %hu\n", addr,
           icmp_hdr_p->type, icmp_hdr_p->code, icmp_hdr_p->un.echo.id,
           icmp_hdr_p->un.echo.sequence);
  }
}
