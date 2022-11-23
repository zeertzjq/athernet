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
#include <time.h>

static uint16_t inet_checksum(const uint16_t *words, int count) {
  uint16_t res = 0;
  while (--count >= 0) {
    uint32_t tmp = res + *words++;
    res = (tmp >> 16) + (tmp & 0xFFFF);
  }
  return ~res;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  char *addr_str = argv[1];
  struct sockaddr_in saddr_dest = {
      .sin_family = AF_INET,
      .sin_port = 0,
  };
  if (inet_pton(AF_INET, addr_str, &saddr_dest.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", addr_str);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
  if (socket_fd < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }

  char send_raw[sizeof(struct iphdr) + sizeof(struct icmphdr)];
  struct iphdr *ip_hdr_p = (struct iphdr *)send_raw;
  *ip_hdr_p = (struct iphdr){
      .ihl = 5,
      .version = 4,
      .ttl = 255,
      .protocol = IPPROTO_ICMP,
      .saddr = 0,
      .daddr = saddr_dest.sin_addr.s_addr,
  };
  char *ip_payload = send_raw + sizeof(struct iphdr);
  struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
  *icmp_hdr_p = (struct icmphdr){
      .type = ICMP_ECHO,
      .code = 0,
      .un.echo.id = htons(11111),
  };
  for (int i = 0; i < 10; i++) {
    icmp_hdr_p->un.echo.sequence = htons(i);
    icmp_hdr_p->checksum = 0;
    icmp_hdr_p->checksum = inet_checksum((uint16_t *)icmp_hdr_p, 4);
    if (sendto(socket_fd, send_raw, sizeof(send_raw), 0,
               (struct sockaddr *)&saddr_dest, sizeof(saddr_dest)) < 0) {
      perror(NULL);
      continue;
    }
    struct timespec ts = {
        .tv_sec = 1,
    };
    nanosleep(&ts, NULL);
  }

  return EXIT_SUCCESS;
}
