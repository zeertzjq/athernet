#include <arpa/inet.h>
#include <errno.h>
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
#include <sys/socket.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

static uint16_t inet_checksum(const uint16_t *words, int count) {
  uint16_t res = 0;
  while (--count >= 0) {
    uint32_t tmp = res + *words++;
    res = (tmp >> 16) + (tmp & 0xFFFF);
  }
  return ~res;
}

static enum {
  NODE_UDP,
  NODE_ICMP,
  NODE_NAT,
} node_type = NODE_UDP;

#define MAC_HEADER_LEN 16
#define MAC_HEADER(dest, src, type, seq)                                       \
  (((dest) << 12) | ((src) << 8) | ((type) << 4) | (seq))

static int mac_self = 1;
static int mac_other = 2;

enum {
  FRAME_DATA = 0,
  FRAME_ACK = 1,
};

static bool mac_send = false;
static bool mac_recv = false;

static int send_seq = 0;
static char send_raw[PHY_PAYLOAD_MAX / 8];
static bool send_bits[PHY_PAYLOAD_MAX];
static size_t send_bits_len = 0;
static uint16_t ack_header_want = 0;
static int64_t time_ack_timeout = 0;

static int64_t time_ping[10];
static int ping_count = 10;
static int ping_done = 0;
static size_t reply_ip_payload_len = 0;

static struct in_addr addr_host;
static struct in_addr addr_dest;
static int port_dest = 0;

static int send_fd = -1;
static int recv_fd = -1;
static int udp_fd = -1;
static int icmp_fd = -1;

static void send_prepare(void) {
  struct iphdr *ip_hdr_p = (struct iphdr *)send_raw;
  *ip_hdr_p = (struct iphdr){
      .ihl = 5,
      .version = 4,
      .ttl = 255,
      .protocol = node_type == NODE_UDP ? IPPROTO_UDP : IPPROTO_ICMP,
      .saddr = addr_host.s_addr,
      .daddr = addr_dest.s_addr,
  };
  char *ip_payload = send_raw + sizeof(struct iphdr);
  size_t raw_payload_len = 0;

  if (node_type == NODE_UDP) {
    struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    if (fgets(udp_payload, sizeof(send_raw) - (udp_payload - send_raw),
              stdin) == NULL) {
      mac_send = false;
      return;
    }
    size_t udp_payload_len = strlen(udp_payload);
    if (udp_payload[udp_payload_len - 1] == '\n') {
      udp_payload[--udp_payload_len] = '\0';
    }
    size_t ip_payload_len = (udp_payload - ip_payload) + udp_payload_len;
    *udp_hdr_p = (struct udphdr){
        .uh_sport = htons(11111),
        .uh_dport = htons(port_dest),
        .uh_ulen = htons(ip_payload_len),
        .uh_sum = 0,
    };
    raw_payload_len = (ip_payload - send_raw) + ip_payload_len;
  } else if (node_type == NODE_ICMP) {
    struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
    size_t ip_payload_len = sizeof(struct icmphdr) + sizeof(struct timeval) + 2;
    if (ping_count >= 0) {
      if (send_seq == ping_count) {
        mac_send = false;
        return;
      }
      if (send_seq > 0 && time_ns() - time_ping[send_seq - 1] < 1000000000) {
        return;
      }
      char *icmp_pad =
          ip_payload + sizeof(struct icmphdr) + sizeof(struct timeval);
      ((uint16_t *)icmp_pad)[0] = htons(11111);
      *icmp_hdr_p = (struct icmphdr){
          .type = ICMP_ECHO,
          .code = 0,
          .checksum = 0,
          .un.echo.sequence = htons(send_seq),
      };
    } else {
      if (reply_ip_payload_len == 0) {
        return;
      }
      ip_payload_len = reply_ip_payload_len;
      reply_ip_payload_len = 0;
      icmp_hdr_p->type = ICMP_ECHOREPLY;
      icmp_hdr_p->checksum = 0;
    }
    icmp_hdr_p->checksum =
        inet_checksum((uint16_t *)icmp_hdr_p, ip_payload_len / 2);
    raw_payload_len = (ip_payload - send_raw) + ip_payload_len;
  } else if (node_type == NODE_NAT) {
    ssize_t len = recvfrom(recv_fd, S_LEN(send_raw), MSG_DONTWAIT, NULL, NULL);
    if (len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror(NULL);
      }
      return;
    }
    struct iphdr *ip_hdr_p = (struct iphdr *)send_raw;
    size_t ip_payload_len = len - sizeof(struct iphdr);
    ip_hdr_p->daddr = addr_host.s_addr;
    if (ip_hdr_p->protocol == IPPROTO_UDP) {
      struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
      if (udp_hdr_p->uh_dport != htons(22222)) {
        return;
      }
      udp_hdr_p->uh_dport = htons(11111);
    } else if (ip_hdr_p->protocol == IPPROTO_ICMP) {
      struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
      char *icmp_pad =
          ip_payload + sizeof(struct icmphdr) + sizeof(struct timeval);
      if (((uint16_t *)icmp_pad)[0] != htons(22222)) {
        return;
      }
      ((uint16_t *)icmp_pad)[0] = htons(11111);
      icmp_hdr_p->checksum = 0;
      icmp_hdr_p->checksum =
          inet_checksum((uint16_t *)icmp_hdr_p, ip_payload_len / 2);
    }
    raw_payload_len = len;
  }
  ip_hdr_p->tot_len = raw_payload_len;

  const uint16_t data_header =
      MAC_HEADER(mac_other, mac_self, FRAME_DATA, send_seq);
  decompose_u16(data_header, send_bits);
  for (size_t i = 0; i < raw_payload_len; i++) {
    decompose_u8(send_raw[i], send_bits + MAC_HEADER_LEN + i * 8);
  }
  send_bits_len = MAC_HEADER_LEN + raw_payload_len * 8;
  ack_header_want = MAC_HEADER(mac_self, mac_other, FRAME_ACK, send_seq);
}

static void mac_send_retry(void) {
  if (node_type == NODE_ICMP) {
    time_ping[send_seq] = time_ns();
  }
  phy_transmit_frame(send_bits, send_bits_len);
  time_ack_timeout = time_ns() + 100000000;
}

static void mac_send_ack(const int seq) {
  const uint16_t ack_header = MAC_HEADER(mac_other, mac_self, FRAME_ACK, seq);
  bool ack_bits[MAC_HEADER_LEN];
  decompose_u16(ack_header, ack_bits);
  phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
}

static void handle_recv(const bool *const bits, const size_t len) {
  char recv_raw[PHY_PAYLOAD_MAX / 8] = {0};
  for (size_t i = 0; i < len; i += 8) {
    recv_raw[i / 8] = compose_u8(bits + i);
  }
  size_t raw_payload_len = len / 8;

  struct iphdr *ip_hdr_p = (struct iphdr *)recv_raw;
  struct in_addr addr_src = {
      .s_addr = ip_hdr_p->saddr,
  };
  char addr[INET_ADDRSTRLEN] = {0};
  if (inet_ntop(AF_INET, &addr_src, addr, sizeof(addr)) == NULL) {
    perror(NULL);
  }
  char *ip_payload = recv_raw + sizeof(struct iphdr);
  size_t ip_payload_len = raw_payload_len - sizeof(struct iphdr);

  if (node_type == NODE_UDP) {
    struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
    char *udp_payload = ip_payload + sizeof(struct udphdr);
    printf("Received IP: %s, Source Port: %hu, Dest Port: %hu, Payload: %s\n",
           addr, ntohs(udp_hdr_p->uh_sport), ntohs(udp_hdr_p->uh_dport),
           udp_payload);
  } else if (node_type == NODE_ICMP) {
    struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
    if (icmp_hdr_p->type == ICMP_ECHOREPLY && ping_count >= 0) {
      uint16_t seq = ntohs(icmp_hdr_p->un.echo.sequence);
      char *icmp_payload = ip_payload + sizeof(struct icmphdr);
      printf("Reply from IP: %s, Seq: %hu, Latency: %lf ms, Payload: %s\n",
             addr, seq, (time_ns() - time_ping[seq]) / 2e6, icmp_payload);
      if (++ping_done == ping_count) {
        mac_recv = false;
      }
    } else if (icmp_hdr_p->type == ICMP_ECHO && ping_count < 0) {
      addr_dest.s_addr = ip_hdr_p->saddr;
      memcpy(send_raw, recv_raw, ip_hdr_p->tot_len);
      reply_ip_payload_len = ip_hdr_p->tot_len - sizeof(struct iphdr);
    }
  } else if (node_type == NODE_NAT) {
    if (ip_hdr_p->saddr != addr_host.s_addr) {
      return;
    }
    ip_hdr_p->saddr = 0;
    if (ip_hdr_p->protocol == IPPROTO_UDP) {
      struct udphdr *udp_hdr_p = (struct udphdr *)ip_payload;
      if (udp_hdr_p->uh_sport != htons(11111)) {
        return;
      }
      udp_hdr_p->uh_sport = htons(22222);
    } else if (ip_hdr_p->protocol == IPPROTO_ICMP) {
      struct icmphdr *icmp_hdr_p = (struct icmphdr *)ip_payload;
      char *icmp_pad =
          ip_payload + sizeof(struct icmphdr) + sizeof(struct timeval);
      if (((uint16_t *)icmp_pad)[0] != htons(11111)) {
        return;
      }
      ((uint16_t *)icmp_pad)[0] = htons(22222);
      icmp_hdr_p->checksum = 0;
      icmp_hdr_p->checksum =
          inet_checksum((uint16_t *)icmp_hdr_p, ip_payload_len / 2);
    }
    struct sockaddr_in saddr_dest = {
        .sin_family = AF_INET,
        .sin_addr = ip_hdr_p->daddr,
        .sin_port = 0,
    };
    if (sendto(send_fd, recv_raw, raw_payload_len, 0,
               (struct sockaddr *)&saddr_dest, sizeof(saddr_dest)) < 0) {
      perror(NULL);
    }
  }
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "send") == 0) {
    mac_send = true;
  } else if (strcmp(argv[1], "recv") == 0) {
    mac_recv = true;
  } else if (strcmp(argv[1], "ping") == 0) {
    node_type = NODE_ICMP;
    mac_send = true;
    mac_recv = true;
  } else if (strcmp(argv[1], "reply") == 0) {
    node_type = NODE_ICMP;
    mac_send = true;
    mac_recv = true;
    ping_count = -1;
  } else if (strcmp(argv[1], "nat") == 0) {
    node_type = NODE_NAT;
    mac_send = true;
    mac_recv = true;
    mac_self = 2;
    mac_other = 1;
    send_fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (send_fd < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    recv_fd = udp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (udp_fd < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    icmp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_fd < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    struct sockaddr_in saddr_bind = {
        .sin_family = AF_INET,
        .sin_port = htons(22222),
        .sin_addr = INADDR_ANY,
    };
    if (bind(udp_fd, (struct sockaddr *)&saddr_bind, sizeof(saddr_bind)) < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
    if (bind(icmp_fd, (struct sockaddr *)&saddr_bind, sizeof(saddr_bind)) < 0) {
      perror(NULL);
      return EXIT_FAILURE;
    }
  } else {
    fprintf(stderr, "Invalid argument: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  if (inet_pton(AF_INET, "192.168.1.2", &addr_host) == 0) {
    fprintf(stderr, "Cannot convert 192.168.1.2\n");
    return EXIT_FAILURE;
  }

  int arg_idx = 2;

  if (mac_send && node_type != NODE_NAT && ping_count >= 0) {
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
      port_dest = atoi(port_str + 1);
      if (port_dest < 0 || port_dest > UINT16_MAX) {
        fprintf(stderr, "Invalid port number: %d\n", port_dest);
        return EXIT_FAILURE;
      }
      *port_str = '\0';
    }

    if (inet_pton(AF_INET, addr_str, &addr_dest) == 0) {
      fprintf(stderr, "Invalid IP address: %s\n", addr_str);
      return EXIT_FAILURE;
    }

    arg_idx = 3;
  }

  for (int i = arg_idx; i < argc; i++) {
    if (node_type == NODE_NAT && strcmp(argv[i], "--icmp") == 0) {
      recv_fd = icmp_fd;
    } else if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
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
  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  const uint16_t recv_ack_header =
      MAC_HEADER(mac_self, mac_other, FRAME_ACK, 0);
  const uint16_t recv_data_header =
      MAC_HEADER(mac_self, mac_other, FRAME_DATA, 0);

  while (mac_send || mac_recv) {
    if (mac_send && ack_header_want == 0) {
      send_prepare();
      if (ack_header_want != 0) {
        mac_send_retry();
      }
    }

    int64_t poll_timeout = 1000000;
    if (phy_poll_frame(&poll_timeout)) {
      bool bits[PHY_PAYLOAD_MAX];
      const size_t len = phy_receive_frame(bits) - MAC_HEADER_LEN;
      const uint16_t header = compose_u16(bits);
      if (mac_recv && (header & 0xFFF0) == recv_data_header) {
        const int seq = header & 0xF;
        mac_send_ack(seq);
        static int recv_seq = 0;
        if (seq == recv_seq) {
          handle_recv(bits + MAC_HEADER_LEN, len);
          recv_seq = (recv_seq + 1) & 0xF;
        }
      } else if (ack_header_want != 0 && header == ack_header_want) {
        send_seq = (send_seq + 1) & 0xF;
        ack_header_want = 0;
      }
      continue;
    }

    if (ack_header_want != 0 && time_ns() > time_ack_timeout) {
      mac_send_retry();
    }
  }

  phy_receive_stopped = 1;
  playback_stop();
  pthread_join(recv_thread, NULL);

  return EXIT_SUCCESS;
}
