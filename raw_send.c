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
      .sin_port = 0,
  };
  if (inet_pton(AF_INET, addr, &dest_addr.sin_addr) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  int socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
  if (socket_fd < 0) {
    perror(NULL);
    return EXIT_FAILURE;
  }

  char ip_payload[sizeof(struct udphdr) + 50];
  char *udp_payload = ip_payload + sizeof(struct udphdr);
  struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
  *udp_hdr_p = (struct udphdr){
      .uh_sport = 0,
      .uh_dport = htons(port),
  };
  while (fgets(udp_payload, 50, stdin) != NULL) {
    size_t udp_payload_len = strlen(udp_payload);
    if (udp_payload[udp_payload_len - 1] == '\n') {
      udp_payload[--udp_payload_len] = '\0';
    }
    size_t ip_payload_len = udp_payload_len + sizeof(struct udphdr);
    udp_hdr_p->uh_ulen = htons(ip_payload_len);
    if (sendto(socket_fd, ip_payload, ip_payload_len, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
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
