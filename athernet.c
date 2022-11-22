#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
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
static char raw_payload[PHY_PAYLOAD_MAX / 8];
static bool transmit_bits[PHY_PAYLOAD_MAX];
static size_t transmit_bits_len = 0;
static uint16_t ack_header_want = 0;
static int num_retries = 0;
static int64_t time_transmit_start = 0;
static int64_t time_transmit_end = 0;

static bool transmit_prepare(void) {
  char *ip_payload = raw_payload + sizeof(struct iphdr);
  size_t raw_payload_len = 0;

  if (node_type == NODE_UDP) {
    struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    if (fgets(udp_payload, 50, stdin) == NULL) {
      return false;
    }
    size_t udp_payload_len = strlen(udp_payload);
    if (udp_payload[udp_payload_len - 1] == '\n') {
      udp_payload[--udp_payload_len] = '\0';
    }
    size_t ip_payload_len = (udp_payload - ip_payload) + udp_payload_len;
    udp_hdr_p->uh_ulen = htons(ip_payload_len);
    raw_payload_len = (udp_payload - raw_payload) + udp_payload_len;
  }

  num_retries = node_type == NODE_ICMP ? 1 : 8;
  const uint16_t data_header =
      MAC_HEADER(mac_other, mac_self, FRAME_DATA, transmit_seq);
  decompose_u16(data_header, transmit_bits);
  for (size_t i = 0; i < raw_payload_len; i++) {
    decompose_u8(raw_payload[i], transmit_bits + MAC_HEADER_LEN + i * 8);
  }
  transmit_bits_len = MAC_HEADER_LEN + raw_payload_len * 8;
  ack_header_want = MAC_HEADER(mac_self, mac_other, FRAME_ACK, transmit_seq);
  return true;
}

static void mac_transmit_retry(void) {
  if (node_type == NODE_ICMP) {
    time_transmit_start = time_ns();
  }
  phy_transmit_frame(transmit_bits, MAC_HEADER_LEN + transmit_bits_len);
  time_transmit_end = time_ns();
}

static void mac_send_ack(const int seq) {
  const uint16_t ack_header = MAC_HEADER(mac_other, mac_self, FRAME_ACK, seq);
  bool ack_bits[MAC_HEADER_LEN];
  decompose_u16(ack_header, ack_bits);
  phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
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

      char *ip_payload = raw_payload + sizeof(struct iphdr);
      struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
      *udp_hdr_p = (struct udphdr){
          .uh_sport = 0,
          .uh_dport = htons(dest_port),
      };
    }

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = 0,
    };
    if (inet_pton(AF_INET, addr_str, &dest_addr.sin_addr) == 0) {
      fprintf(stderr, "Invalid IP address: %s\n", addr_str);
      return EXIT_FAILURE;
    }

    struct iphdr *ip_hdr_p = (struct iphdr *)raw_payload;
    *ip_hdr_p = (struct iphdr){
        .ihl = 5,
        .version = 4,
        .ttl = 255,
        .protocol = node_type == NODE_UDP ? IPPROTO_UDP : IPPROTO_ICMP,
        .daddr = dest_addr.sin_addr.s_addr,
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

  const int64_t ack_timeout = node_type == NODE_ICMP ? 2000000000 : 100000000;

  if (transmit) {
    transmit = transmit_prepare();
    if (transmit) {
      mac_transmit_retry();
    }
  }

  const uint16_t receive_ack_header =
      MAC_HEADER(mac_self, mac_other, FRAME_ACK, 0);
  const uint16_t receive_data_header =
      MAC_HEADER(mac_self, mac_other, FRAME_DATA, 0);

  while (transmit || receive) {
    int64_t poll_timeout = 10000000;
    if (phy_poll_frame(&poll_timeout)) {
      bool bits[PHY_PAYLOAD_MAX];
      const size_t len = phy_receive_frame(bits) - MAC_HEADER_LEN;
      const uint16_t header = compose_u16(bits);
      if (receive && (header & 0xFFF0) == receive_data_header) {
        const int seq = header & 0xF;
        mac_send_ack(seq);
        if (node_type == NODE_UDP) {
          static int receive_seq = 0;
          if (seq == receive_seq) {
            for (size_t pos = 0; pos < len; pos += 8) {
              putchar(compose_u8(bits + pos));
            }
            fflush(stdout);
            receive_seq = (receive_seq + 1) & 0xF;
          }
        }
      } else if (transmit && header == ack_header_want) {
        if (node_type == NODE_ICMP) {
          fprintf(stderr, "%lf ms\n", (time_ns() - time_transmit_start) / 1e6);
        }
        transmit_seq = (transmit_seq + 1) & 0xF;
        transmit = transmit_prepare();
        if (transmit) {
          mac_transmit_retry();
        }
      }
      continue;
    }

    const int64_t time_now = time_ns();

    if (transmit && time_now - time_transmit_end > ack_timeout) {
      if (num_retries == 0) {
        if (node_type == NODE_UDP) {
          fprintf(stderr, "link error\n");
        } else {
          if (node_type == NODE_ICMP) {
            fprintf(stderr, "TIMEOUT\n");
          }
        }
        transmit = transmit_prepare();
        if (transmit) {
          mac_transmit_retry();
        }
      } else {
        mac_transmit_retry();
      }
    }
  }

  phy_receive_stopped = 1;
  playback_stop();
  pthread_join(receive_thread, NULL);

  return EXIT_SUCCESS;
}
