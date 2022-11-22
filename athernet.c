#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

static enum {
  NODE_UDP,
  NODE_ICMP,
} node_type = NODE_UDP;

#define MAC_HEADER_LEN 16
#define MAC_HEADER(dest, src, type, seq)                                       \
  (((dest) << 12) | ((src) << 8) | ((type) << 4) | (seq))

static int mac_self = 2;
static int mac_other = 1;

enum {
  FRAME_DATA = 0,
  FRAME_ACK = 1,
};

static int transmit_seq = 0;
static char send_raw[PHY_PAYLOAD_MAX / 8];
static bool transmit_bits[PHY_PAYLOAD_MAX];
static size_t transmit_bits_len = 0;
static uint16_t ack_header_want = 0;
static int64_t time_transmit_start = 0;
static int64_t time_ack_timeout = 0;

static void transmit_prepare(void) {
  char *ip_payload = send_raw + sizeof(struct iphdr);
  size_t raw_len = 0;

  if (node_type == NODE_UDP) {
    struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    if (fgets(udp_payload, 50, stdin) == NULL) {
      return;
    }
    size_t udp_payload_len = strlen(udp_payload);
    if (udp_payload[udp_payload_len - 1] == '\n') {
      udp_payload[--udp_payload_len] = '\0';
    }
    size_t ip_payload_len = (udp_payload - ip_payload) + udp_payload_len;
    udp_hdr_p->uh_ulen = htons(ip_payload_len);
    raw_len = (udp_payload - send_raw) + udp_payload_len;
  } else if (node_type == NODE_ICMP) {
    struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
    *icmp_hdr_p = (struct icmphdr){
        .type = ICMP_ECHO,
    };
  }

  const uint16_t data_header =
      MAC_HEADER(mac_other, mac_self, FRAME_DATA, transmit_seq);
  decompose_u16(data_header, transmit_bits);
  for (size_t i = 0; i < raw_len; i++) {
    decompose_u8(send_raw[i], transmit_bits + MAC_HEADER_LEN + i * 8);
  }
  transmit_bits_len = MAC_HEADER_LEN + raw_len * 8;
  ack_header_want = MAC_HEADER(mac_self, mac_other, FRAME_ACK, transmit_seq);
}

static void mac_transmit_retry(void) {
  if (node_type == NODE_ICMP) {
    time_transmit_start = time_ns();
  }
  phy_transmit_frame(transmit_bits, MAC_HEADER_LEN + transmit_bits_len);
  time_ack_timeout = time_ns() + 100000000;
}

static void mac_send_ack(const int seq) {
  const uint16_t ack_header = MAC_HEADER(mac_other, mac_self, FRAME_ACK, seq);
  bool ack_bits[MAC_HEADER_LEN];
  decompose_u16(ack_header, ack_bits);
  phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
}

static void handle_recv(const bool *const bits, const size_t len) {
  static char recv_raw[PHY_PAYLOAD_MAX / 8];
  for (size_t i = 0; i < len; i += 8) {
    recv_raw[i / 8] = compose_u8(bits + i);
  }

  struct iphdr *ip_hdr_p = (struct iphdr *)recv_raw;
  struct in_addr addr_src = {
      .s_addr = ip_hdr_p->saddr,
  };
  char addr[INET_ADDRSTRLEN] = {0};
  if (inet_ntop(AF_INET, &addr_src, addr, sizeof(addr)) == NULL) {
    perror(NULL);
  }
  char *ip_payload = recv_raw + sizeof(struct iphdr);

  if (node_type == NODE_UDP) {
    struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    printf("Received IP: %s, Source Port: %hu, Dest Port: %hu, Payload: %s\n",
           addr, ntohs(udp_hdr_p->uh_sport), ntohs(udp_hdr_p->uh_dport),
           udp_payload);
  } else if (node_type == NODE_ICMP) {
    struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
    (void)icmp_hdr_p;
  }
}

int main(int argc, char **argv) {
  bool transmit = false;
  bool receive = false;

  if (argc <= 1) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "send") == 0) {
    transmit = true;
  } else if (strcmp(argv[1], "recv") == 0) {
    receive = true;
  } else if (strcmp(argv[1], "ping") == 0) {
    node_type = NODE_ICMP;
    transmit = true;
    receive = true;
  } else {
    fprintf(stderr, "Invalid argument: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  if (transmit) {
    if (argc <= 2) {
      fprintf(stderr, "Missing argument\n");
      return EXIT_FAILURE;
    }

    char *addr_str = argv[2];

    if (node_type == NODE_UDP) {
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

      char *ip_payload = send_raw + sizeof(struct iphdr);
      struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
      *udp_hdr_p = (struct udphdr){
          .uh_sport = 0,
          .uh_dport = htons(dest_port),
      };
    }

    struct in_addr addr_dest;
    if (inet_pton(AF_INET, addr_str, &addr_dest) == 0) {
      fprintf(stderr, "Invalid IP address: %s\n", addr_str);
      return EXIT_FAILURE;
    }

    struct iphdr *ip_hdr_p = (struct iphdr *)send_raw;
    *ip_hdr_p = (struct iphdr){
        .ihl = 5,
        .version = 4,
        .ttl = 255,
        .protocol = node_type == NODE_UDP ? IPPROTO_UDP : IPPROTO_ICMP,
        .daddr = addr_dest.s_addr,
    };
  }

  for (int i = transmit ? 3 : 2; i < argc; i++) {
    if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else if (strncmp(argv[i], S_LEN("--self=")) == 0) {
      mac_self = atoi(argv[i] + LEN("--self=")) & 0xF;
    } else if (strncmp(argv[i], S_LEN("--other=")) == 0) {
      mac_other = atoi(argv[i] + LEN("--other=")) & 0xF;
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  phy_init();
  pthread_t receive_thread;
  pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  const uint16_t receive_ack_header =
      MAC_HEADER(mac_self, mac_other, FRAME_ACK, 0);
  const uint16_t receive_data_header =
      MAC_HEADER(mac_self, mac_other, FRAME_DATA, 0);

  while (transmit || receive) {
    if (transmit && ack_header_want == 0) {
      transmit_prepare();
      if (ack_header_want != 0) {
        mac_transmit_retry();
      } else if (node_type == NODE_UDP) {
        transmit = false;
      }
    }

    int64_t poll_timeout = 10000000;
    if (phy_poll_frame(&poll_timeout)) {
      bool bits[PHY_PAYLOAD_MAX];
      const size_t len = phy_receive_frame(bits) - MAC_HEADER_LEN;
      const uint16_t header = compose_u16(bits);
      if (receive && (header & 0xFFF0) == receive_data_header) {
        const int seq = header & 0xF;
        mac_send_ack(seq);
        static int receive_seq = 0;
        if (seq == receive_seq) {
          handle_recv(bits + MAC_HEADER_LEN, len);
          receive_seq = (receive_seq + 1) & 0xF;
        }
      } else if (ack_header_want != 0 && header == ack_header_want) {
        transmit_seq = (transmit_seq + 1) & 0xF;
        ack_header_want = 0;
      }
      continue;
    }

    if (ack_header_want != 0 && time_ns() > time_ack_timeout) {
      mac_transmit_retry();
    }
  }

  phy_receive_stopped = 1;
  playback_stop();
  pthread_join(receive_thread, NULL);

  return EXIT_SUCCESS;
}
