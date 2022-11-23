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
#include <time.h>

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  char *addr_str = argv[1];
  char *port_str = strchr(addr_str, ':');
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

  char send_raw[sizeof(struct iphdr) + sizeof(struct udphdr) + 50];
  struct iphdr *ip_hdr_p = (struct iphdr *)send_raw;
  *ip_hdr_p = (struct iphdr){
      .ihl = 5,
      .version = 4,
      .ttl = 255,
      .protocol = IPPROTO_UDP,
      .saddr = 0,
      .daddr = saddr_dest.sin_addr.s_addr,
  };
  char *ip_payload = send_raw + sizeof(struct iphdr);
  struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
  *udp_hdr_p = (struct udphdr){
      .uh_sport = htons(33333),
      .uh_dport = htons(dest_port),
  };
  char *udp_payload = ip_payload + sizeof(struct udphdr);
  while (fgets(udp_payload, 50, stdin) != NULL) {
    size_t udp_payload_len = strlen(udp_payload);
    if (udp_payload[udp_payload_len - 1] == '\n') {
      udp_payload[--udp_payload_len] = '\0';
    }
    size_t ip_payload_len = (udp_payload - ip_payload) + udp_payload_len;
    udp_hdr_p->uh_ulen = htons(ip_payload_len);
    size_t raw_payload_len = (ip_payload - send_raw) + ip_payload_len;
    if (sendto(socket_fd, send_raw, raw_payload_len, 0,
               (struct sockaddr *)&saddr_dest, sizeof(saddr_dest)) < 0) {
      perror(NULL);
      continue;
    }
    printf("Sent Payload: %s\n", udp_payload);
    struct timespec ts = {
        .tv_nsec = 500000000,
    };
    nanosleep(&ts, NULL);
  }

  return EXIT_SUCCESS;
}
