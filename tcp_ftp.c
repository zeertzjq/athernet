#include <arpa/inet.h>
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
const struct iphdr *const ip_send_hdr_p[2] = {
    (struct iphdr *)raw_send_payload[0],
    (struct iphdr *)raw_send_payload[1],
};
const struct iphdr *const ip_recv_hdr_p = (struct iphdr *)raw_recv_payload;
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
  if (tcp_recv_hdr_p->th_flags & TH_SYN) {
    if (tcp_state[d] == TCP_SYN_SENT) {
      tcp_state[d] = TCP_ESTABLISHED;
    } else if (tcp_state[d] != TCP_ESTABLISHED) {
      return;
    }
  }
  const size_t tcp_recv_header_len = (tcp_recv_hdr_p->th_off << 2);
  const char *tcp_recv_payload = ip_recv_payload + tcp_recv_header_len;
  const size_t tcp_recv_payload_len =
      (ip_recv_hdr_p->tot_len - sizeof(struct iphdr) - tcp_recv_header_len);
  if (tcp_recv_payload_len != 0) {
    if (tcp_recv_hdr_p->th_seq == tcp_want_seq[d]) {
      fwrite(tcp_recv_payload, 1, tcp_recv_payload_len, tcp_output_stream[d]);
    }
  }
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
      sleep_ns(1000);
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

  tcp_output_stream[0] = stdout;

  pthread_t input_thread;
  pthread_create(&input_thread, NULL, input_loop, NULL);

  while (tcp_state[0] != TCP_CLOSE || tcp_state[1] != TCP_CLOSE ||
         !input_stopped) {
    if (!input_stopped && tcp_state[0] == TCP_CLOSE) {
    }
  }

  pthread_join(input_thread, NULL);

  return 0;
}
