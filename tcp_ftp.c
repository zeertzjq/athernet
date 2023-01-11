#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define RAW_SEND_MAX 600
#define RAW_RECV_MAX 640
static struct in_addr addr_host;
static struct in_addr addr_dest;
static int ip_send_fd = -1;
static int ip_recv_fd = -1;

static char raw_send_payload[2][RAW_SEND_MAX];
static char raw_recv_payload[RAW_RECV_MAX];
static size_t raw_send_len[2] = {0, 0};
static size_t raw_recv_len = 0;
static struct iphdr *const ip_send_hdr_p[2] = {
    (struct iphdr *)raw_send_payload[0],
    (struct iphdr *)raw_send_payload[1],
};
struct iphdr *const ip_recv_hdr_p = (struct iphdr *)raw_recv_payload;
static char *const ip_send_payload[2] = {
    raw_send_payload[0] + sizeof(struct iphdr),
    raw_send_payload[1] + sizeof(struct iphdr),
};
static char *const ip_recv_payload = raw_recv_payload + sizeof(struct iphdr);
static struct tcphdr *const tcp_send_hdr_p[2] = {
    (struct tcphdr *)(raw_send_payload[0] + sizeof(struct iphdr)),
    (struct tcphdr *)(raw_send_payload[1] + sizeof(struct iphdr)),
};
static struct tcphdr *const tcp_recv_hdr_p =
    (struct tcphdr *)(raw_recv_payload + sizeof(struct iphdr));
static char *const tcp_send_payload[2] = {
    raw_send_payload[0] + sizeof(struct iphdr) + sizeof(struct tcphdr),
    raw_send_payload[1] + sizeof(struct iphdr) + sizeof(struct tcphdr),
};
static int64_t tcp_ack_timeout[2] = {0, 0};
static tcp_seq tcp_want_seq[2] = {0, 0};
static FILE *tcp_output_stream[2] = {NULL, NULL};
static int tcp_state[2] = {TCP_CLOSE, TCP_CLOSE};

static void tcp_fill_checksum(const bool d) {
  tcp_send_hdr_p[d]->th_sum = 0;
  const uint16_t tcp_len = raw_send_len[d] - sizeof(struct iphdr);
  struct {
    uint32_t saddr, daddr;
    uint8_t zeros, protocol;
    uint16_t tcp_len;
  } pseudo_hdr = {
      .saddr = ip_send_hdr_p[d]->saddr,
      .daddr = ip_send_hdr_p[d]->daddr,
      .protocol = IPPROTO_TCP,
      .tcp_len = htons(tcp_len),
  };
  uint32_t sum = 0;
  for (size_t i = 0; i < sizeof(pseudo_hdr) / 2; i++) {
    sum += ((uint16_t *)&pseudo_hdr)[i];
  }
  for (size_t i = 0; i < tcp_len / 2; i++) {
    sum += ((uint16_t *)ip_send_payload[d])[i];
  }
  if (tcp_len & 1) {
    sum += ((uint8_t *)ip_send_payload[d])[tcp_len - 1];
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  tcp_send_hdr_p[d]->th_sum = ~sum;
}

static void tcp_syn_prepare(const bool d) {
  *ip_send_hdr_p[d] = (struct iphdr){
      .ihl = 5,
      .version = 4,
      .ttl = 255,
      .protocol = IPPROTO_TCP,
      .saddr = addr_host.s_addr,
      .daddr = addr_dest.s_addr,
  };
  *tcp_send_hdr_p[d] = (struct tcphdr){
      .th_sport = htons(d ? 11110 : 11111),
      .th_dport = htons(d ? 20 : 21),
      .th_seq = htonl(rand()),
      .th_off = 5,
      .th_flags = TH_SYN,
      .th_win = htons(1),
  };
  raw_send_len[d] = sizeof(struct iphdr) + sizeof(struct tcphdr);
  tcp_fill_checksum(d);
  tcp_ack_timeout[d] = 0;
  tcp_state[d] = TCP_SYN_SENT;
}

static void tcp_handle_recv(void) {
  if (ip_recv_hdr_p->saddr != addr_dest.s_addr) {
    return;
  }
  const uint16_t port_src = ntohs(tcp_recv_hdr_p->th_sport);
  if (port_src != 21 && port_src != 20) {
    return;
  }
  const bool d = port_src == 20;
  if (tcp_state[d] == TCP_CLOSE) {
    return;
  }
  if (tcp_recv_hdr_p->th_flags & TH_RST) {
    raw_send_len[d] = 0;
    tcp_state[d] = TCP_CLOSE;
    return;
  }
  if (!(tcp_recv_hdr_p->th_flags & TH_ACK)) {
    return;
  }
  const size_t tcp_recv_header_len = (tcp_recv_hdr_p->th_off << 2);
  const char *tcp_recv_payload = ip_recv_payload + tcp_recv_header_len;
  const size_t tcp_recv_len =
      raw_recv_len - sizeof(struct iphdr) - tcp_recv_header_len;
  bool need_reply = true;
  if (tcp_recv_hdr_p->th_flags & TH_SYN) {
    if (tcp_state[d] == TCP_SYN_SENT) {
      tcp_state[d] = TCP_ESTABLISHED;
      tcp_send_hdr_p[d]->th_flags = TH_ACK;
    } else if (raw_send_len[d] != 0 || tcp_state[d] != TCP_ESTABLISHED) {
      return;
    }
  } else if (tcp_recv_hdr_p->th_seq != htonl(tcp_want_seq[d])) {
    return;
  } else if (tcp_recv_hdr_p->th_flags & TH_FIN) {
    if (tcp_state[d] == TCP_ESTABLISHED) {
      tcp_state[d] = TCP_CLOSE_WAIT;
    } else if (tcp_state[d] == TCP_FIN_WAIT1) {
      tcp_state[d] = TCP_CLOSING;
    } else if (tcp_state[d] == TCP_FIN_WAIT2) {
      tcp_state[d] = TCP_TIME_WAIT;
    }
  } else {
    if (tcp_state[d] == TCP_FIN_WAIT1) {
      tcp_state[d] = TCP_FIN_WAIT2;
    } else if (tcp_state[d] == TCP_CLOSING) {
      tcp_state[d] = TCP_TIME_WAIT;
    } else if (tcp_state[d] == TCP_LAST_ACK) {
      tcp_state[d] = TCP_CLOSE;
      return;
    }
    if (tcp_recv_len == 0) {
      need_reply = false;
    }
  }
  raw_send_len[d] = 0;
  if (tcp_recv_len != 0) {
    fwrite(tcp_recv_payload, 1, tcp_recv_len, tcp_output_stream[d]);
  }
  tcp_want_seq[d] = ntohl(tcp_recv_hdr_p->th_seq) + 1;
  tcp_send_hdr_p[d]->th_seq = tcp_recv_hdr_p->th_ack;
  tcp_send_hdr_p[d]->th_ack = htonl(tcp_want_seq[d]);
  if (need_reply) {
    raw_send_len[d] = sizeof(struct iphdr) + sizeof(struct tcphdr);
    tcp_fill_checksum(d);
    tcp_ack_timeout[d] = 0;
  }
}

static bool tcp_need_ack(const bool d) {
  return raw_send_len[d] > sizeof(struct iphdr) + sizeof(struct tcphdr) ||
         tcp_send_hdr_p[d]->th_flags != TH_ACK;
}

static bool tcp_need_retry(const bool d) {
  return tcp_ack_timeout[d] == 0 ||
         tcp_need_ack(d) && time_ns() > tcp_ack_timeout[d];
}

static const char *const ftp_cmd_name[] = {
    "USER", "PASS", "PWD", "CWD", "PASV", "LIST", "RETR",
};
static size_t ftp_cmd_matched[ARRAY_SIZE(ftp_cmd_name)][4] = {0};

static void ftp_parse_reset(void) {
  memset(ftp_cmd_matched, 0, sizeof(ftp_cmd_matched));
}

static void ftp_parse_add(int c) {
  if (c >= 'a' && c <= 'z') {
    c += 'A' - 'a';
  } else if (c < 'A' || c > 'Z') {
    return;
  }
  for (size_t cmd_idx = 0; cmd_idx < ARRAY_SIZE(ftp_cmd_name); cmd_idx++) {
    const char *const cmd = ftp_cmd_name[cmd_idx];
    const size_t cmd_len = strlen(cmd);
    size_t *const matched_old = ftp_cmd_matched[cmd_idx];
    size_t matched_new[4] = {0};
    if (c == cmd[0]) {
      matched_new[0] = 1;
    }
    for (size_t i = 1; i < cmd_len; i++) {
      if (cmd[i] == c) {
        matched_new[i] = matched_old[i - 1] + 1;
      } else {
        if (matched_new[i - 1] > matched_old[i]) {
          matched_new[i] = matched_new[i - 1];
        } else {
          matched_new[i] = matched_old[i];
        }
      }
    }
    memcpy(matched_old, matched_new, sizeof(matched_new));
  }
}

static const char *ftp_parse_get(void) {
  size_t max_matched = 0;
  const char *max_matched_cmd = NULL;
  for (size_t cmd_idx = 0; cmd_idx < ARRAY_SIZE(ftp_cmd_name); cmd_idx++) {
    const char *const cmd = ftp_cmd_name[cmd_idx];
    const size_t len = ftp_cmd_matched[cmd_idx][strlen(cmd) - 1];
    if (len > max_matched) {
      max_matched = len;
      max_matched_cmd = cmd;
    } else if (len == max_matched) {
      max_matched_cmd = NULL;
    }
  }
  if (max_matched_cmd != NULL) {
    return max_matched_cmd;
  }
  printf("Do you mean:");
  for (size_t cmd_idx = 0; cmd_idx < ARRAY_SIZE(ftp_cmd_name); cmd_idx++) {
    const char *const cmd = ftp_cmd_name[cmd_idx];
    const size_t len = ftp_cmd_matched[cmd_idx][strlen(cmd) - 1];
    if (len == max_matched) {
      printf(" %s", ftp_cmd_name[cmd_idx]);
    }
  }
  puts("");
  return NULL;
}

#define FTP_CMD_MAXLEN 400
static char ftp_cmd[FTP_CMD_MAXLEN];
static volatile sig_atomic_t ftp_cmd_len = 0;
static volatile sig_atomic_t input_stopped = 0;

static void *input_loop(void *args) {
  bool past_cmd = false;
  const char *cmd = NULL;
  size_t cmd_len = 0;
  for (;;) {
    while (ftp_cmd_len > 0) {
      sleep_ns(1000000);
    }
    const int c = getchar();
    if (c == EOF) {
      input_stopped = 1;
      return NULL;
    }
    if (!past_cmd) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        past_cmd = true;
        cmd = ftp_parse_get();
        ftp_parse_reset();
        if (cmd != NULL) {
          strncpy(ftp_cmd, cmd, FTP_CMD_MAXLEN);
          cmd_len = strlen(cmd);
        }
      } else {
        ftp_parse_add(c);
      }
    }
    if (cmd != NULL && cmd_len < FTP_CMD_MAXLEN) {
      ftp_cmd[cmd_len++] = c;
    }
    if (c == '\n') {
      if (cmd != NULL) {
        ftp_cmd_len = cmd_len;
      }
      past_cmd = false;
      cmd = NULL;
    }
  }
}

int main(int argc, char **argv) {
  if (argc <= 2) {
    fprintf(stderr, "Not enough arguments\n");
    return EXIT_FAILURE;
  }
  if (inet_pton(AF_INET, argv[1], &addr_host) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
    return EXIT_FAILURE;
  }
  if (inet_pton(AF_INET, argv[2], &addr_dest) == 0) {
    fprintf(stderr, "Invalid IP address: %s\n", argv[2]);
    return EXIT_FAILURE;
  }

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
      .sin_addr = addr_host,
  };
  if (bind(ip_recv_fd, (struct sockaddr *)&saddr_bind, sizeof(saddr_bind)) <
      0) {
    perror(NULL);
    return EXIT_FAILURE;
  }
  struct sockaddr_in saddr_dest = {
      .sin_family = AF_INET,
      .sin_addr = addr_dest,
      .sin_port = 0,
  };

  srand(time_ns());
  tcp_output_stream[0] = stdout;

  pthread_t input_thread;
  pthread_create(&input_thread, NULL, input_loop, NULL);

  while (tcp_state[0] != TCP_CLOSE || tcp_state[1] != TCP_CLOSE ||
         !input_stopped) {
    if (!input_stopped && tcp_state[0] == TCP_CLOSE) {
      tcp_syn_prepare(0);
    }
    for (int d = 0; d <= 1; d++) {
      if (raw_send_len[d] > 0 && tcp_need_retry(d)) {
        sendto(ip_send_fd, raw_send_payload[d], raw_send_len[d], 0,
               (struct sockaddr *)&saddr_dest, sizeof(saddr_dest));
        if (tcp_need_ack(d)) {
          tcp_ack_timeout[d] = time_ns() + 1000000000;
        } else {
          raw_send_len[d] = 0;
        }
      }
    }
    if (raw_send_len[0] == 0 && raw_send_len[1] == 0 && ftp_cmd_len > 0) {
      memcpy(tcp_send_payload[0], ftp_cmd, ftp_cmd_len);
      raw_send_len[0] =
          sizeof(struct iphdr) + sizeof(struct tcphdr) + ftp_cmd_len;
      tcp_fill_checksum(0);
      tcp_ack_timeout[0] = 0;
    }
    sleep_ns(1000000);
    ssize_t recv_len =
        recvfrom(ip_recv_fd, S_LEN(raw_recv_payload), MSG_DONTWAIT, NULL, NULL);
    if (recv_len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror(NULL);
      }
    } else {
      raw_recv_len = recv_len;
      tcp_handle_recv();
    }
  }

  pthread_join(input_thread, NULL);

  return 0;
}
