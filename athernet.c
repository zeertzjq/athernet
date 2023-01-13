#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "backend.h"
#include "common.h"
#include "ftp.h"
#include "physical.h"
#include "tcp_ip.h"

static enum {
  NODE_FTP,
  NODE_NAT,
} node_type = NODE_FTP;

#define MAC_HEADER_LEN 16
#define MAC_HEADER(dest, src, type, seq)                                       \
  (((dest) << 12) | ((src) << 8) | ((type) << 4) | (seq))

static int mac_self = 1;
static int mac_other = 2;

enum {
  FRAME_DATA = 0,
  FRAME_ACK = 1,
};

static bool mac_send_d = false;
static int mac_send_seq = 0;
static bool mac_send_bits[PHY_PAYLOAD_MAX];
static bool mac_recv_bits[PHY_PAYLOAD_MAX];
static size_t mac_send_bits_len = 0;
static size_t mac_recv_bits_len = 0;
static uint16_t mac_ack_want = 0;
static int64_t mac_ack_timeout = 0;

static struct in_addr addr_nat;
static uint16_t port_nat[2] = {0, 0};

static int ip_send_fd = -1;
static int ip_recv_fd = -1;

static void mac_send_prepare(void) {
  if (node_type == NODE_FTP) {
    if (!ftp_input_stopped && tcp_state[0] == TCP_CLOSE) {
      tcp_prepare_syn(0);
    } else if (ftp_input_stopped && tcp_state[0] == TCP_ESTABLISHED) {
      tcp_prepare_fin(0);
    }
    int d;
    for (d = 1; d >= 0; d--) {
      if (tcp_state[d] == TCP_CLOSE) {
        continue;
      }
      if (raw_send_len[d] > 0 && tcp_need_retry(d)) {
        mac_send_d = d;
        break;
      } else if (raw_send_len[d] == 0 && tcp_state[d] == TCP_CLOSE_WAIT) {
        tcp_prepare_fin(d);
      } else if (tcp_state[d] == TCP_TIME_WAIT && time_ns() > tcp_timeout[d]) {
        tcp_close(d);
      }
    }
    if (raw_send_len[0] == 0 && raw_send_len[1] == 0) {
      ftp_prepare_cmd();
    }
    if (d < 0) {
      return;
    }
  } else if (node_type == NODE_NAT) {
    ssize_t raw_len = recvfrom(ip_recv_fd, raw_send_payload[mac_send_d],
                               RAW_PAYLOAD_MAX, MSG_DONTWAIT, NULL, NULL);
    if (raw_len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror(NULL);
      }
      return;
    }
    if (ip_send_hdr_p[mac_send_d]->ihl != 5 ||
        ip_send_hdr_p[mac_send_d]->protocol != IPPROTO_TCP) {
      return;
    }
    ip_send_hdr_p[mac_send_d]->daddr = addr_host.s_addr;
    if (tcp_recv_hdr_p->th_sport == htons(port_nat[0])) {
      tcp_recv_hdr_p->th_sport = htons(port_host[0]);
    } else if (tcp_recv_hdr_p->th_sport == htons(port_nat[1])) {
      tcp_recv_hdr_p->th_sport = htons(port_host[1]);
    } else {
      return;
    }
    raw_send_len[mac_send_d] = raw_len;
    tcp_fill_checksum(raw_recv_payload, raw_send_len[mac_send_d]);
  }
  ip_send_hdr_p[mac_send_d]->tot_len = raw_send_len[mac_send_d];

  const uint16_t data_header =
      MAC_HEADER(mac_other, mac_self, FRAME_DATA, mac_send_seq);
  decompose_u16(data_header, mac_send_bits);
  for (size_t i = 0; i < raw_send_len[mac_send_d]; i++) {
    decompose_u8(raw_send_payload[mac_send_d][i],
                 mac_send_bits + MAC_HEADER_LEN + i * 8);
  }
  mac_send_bits_len = MAC_HEADER_LEN + raw_send_len[mac_send_d] * 8;
  mac_ack_want = MAC_HEADER(mac_self, mac_other, FRAME_ACK, mac_send_seq);
}

static void mac_send_retry(void) {
  phy_transmit_frame(mac_send_bits, mac_send_bits_len);
  mac_ack_timeout = time_ns() + 100000000;
}

static void mac_send_ack(const int seq) {
  const uint16_t ack_header = MAC_HEADER(mac_other, mac_self, FRAME_ACK, seq);
  bool ack_bits[MAC_HEADER_LEN];
  decompose_u16(ack_header, ack_bits);
  phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
}

static void mac_handle_recv(void) {
  const bool *const mac_payload_bits = mac_recv_bits + MAC_HEADER_LEN;
  const size_t mac_payload_bits_len = mac_recv_bits_len - MAC_HEADER_LEN;
  for (size_t i = 0; i < mac_payload_bits_len; i += 8) {
    raw_recv_payload[i / 8] = compose_u8(mac_payload_bits + i);
  }
  raw_recv_len = mac_payload_bits_len / 8;

  if (node_type == NODE_FTP) {
    for (int d = 1; d >= 0; d--) {
      if (tcp_recv_check(d)) {
        ssize_t tcp_recv_len = tcp_handle_recv(d);
        if (d == 0 && tcp_recv_len >= 0) {
          ftp_handle_reply(tcp_recv_len);
        }
        tcp_interrupted[d] = false;
        break;
      }
    }
  } else if (node_type == NODE_NAT) {
    if (ip_recv_hdr_p->saddr != addr_host.s_addr ||
        ip_recv_hdr_p->protocol != IPPROTO_TCP) {
      return;
    }
    ip_recv_hdr_p->saddr = addr_nat.s_addr;
    if (tcp_recv_hdr_p->th_sport == htons(port_host[0])) {
      tcp_recv_hdr_p->th_sport = htons(port_nat[0]);
    } else if (tcp_recv_hdr_p->th_sport == htons(port_host[1])) {
      tcp_recv_hdr_p->th_sport = htons(port_nat[1]);
    } else {
      return;
    }
    tcp_fill_checksum(raw_recv_payload, raw_recv_len);
    struct sockaddr_in saddr_dest = {
        .sin_family = AF_INET,
        .sin_addr = ip_recv_hdr_p->daddr,
        .sin_port = 0,
    };
    if (sendto(ip_send_fd, raw_recv_payload, raw_recv_len, 0,
               (struct sockaddr *)&saddr_dest, sizeof(saddr_dest)) < 0) {
      perror(NULL);
    }
  }
}

int main(int argc, char **argv) {
  if (argc <= 2) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "nat") == 0) {
    node_type = NODE_NAT;
    mac_self = 2;
    mac_other = 1;
  } else if (strcmp(argv[1], "ftp") != 0) {
    fprintf(stderr, "Invalid argument: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  if (inet_pton(AF_INET, "192.168.1.2", &addr_host) == 0) {
    fprintf(stderr, "Cannot convert 192.168.1.2\n");
    return EXIT_FAILURE;
  }

  if (inet_pton(AF_INET, argv[2],
                node_type == NODE_NAT ? &addr_nat : &addr_dest[0]) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", argv[2]);
    return EXIT_FAILURE;
  }

  if (node_type == NODE_NAT) {
    ip_send_fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (ip_send_fd < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    ip_recv_fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (ip_recv_fd < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    struct sockaddr_in saddr_bind = {
        .sin_family = AF_INET,
        .sin_port = 0,
        .sin_addr = addr_nat,
    };
    if (bind(ip_recv_fd, (struct sockaddr *)&saddr_bind, sizeof(saddr_bind)) <
        0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
  }

  for (int i = 3; i < argc; i++) {
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

  srand(time_ns());
  port_host[0] = 11111;
  port_host[1] = 11110;
  if (node_type == NODE_NAT) {
    port_nat[0] = rand();
    port_nat[1] = rand();
  }

  pthread_t ftp_input_thread;
  if (node_type == NODE_FTP) {
    pthread_create(&ftp_input_thread, NULL, ftp_input_loop, NULL);
  }

  phy_init();
  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  const uint16_t recv_ack_header =
      MAC_HEADER(mac_self, mac_other, FRAME_ACK, 0);
  const uint16_t recv_data_header =
      MAC_HEADER(mac_self, mac_other, FRAME_DATA, 0);

  while (node_type == NODE_NAT ||
         (tcp_state[0] != TCP_CLOSE || tcp_state[1] != TCP_CLOSE ||
          !ftp_input_stopped)) {
    if (mac_ack_want == 0) {
      mac_send_prepare();
      if (mac_ack_want != 0) {
        mac_send_retry();
      }
    }

    int64_t poll_timeout = 1000000;
    if (phy_poll_frame(&poll_timeout)) {
      mac_recv_bits_len = phy_receive_frame(mac_recv_bits);
      const uint16_t header = compose_u16(mac_recv_bits);
      if ((header & 0xFFF0) == recv_data_header) {
        const int seq = header & 0xF;
        mac_send_ack(seq);
        static int recv_seq = 0;
        if (seq == recv_seq) {
          mac_handle_recv();
          recv_seq = (recv_seq + 1) & 0xF;
        }
      } else if (mac_ack_want != 0 && header == mac_ack_want) {
        mac_send_seq = (mac_send_seq + 1) & 0xF;
        mac_ack_want = 0;
        if (node_type == NODE_FTP) {
          tcp_after_send(mac_send_d);
        }
      }
      continue;
    }

    if (mac_ack_want != 0 && time_ns() > mac_ack_timeout) {
      mac_send_retry();
    }
  }

  phy_receive_stopped = 1;
  playback_stop();
  pthread_join(recv_thread, NULL);

  if (node_type == NODE_FTP) {
    pthread_join(ftp_input_thread, NULL);
  }

  return EXIT_SUCCESS;
}
