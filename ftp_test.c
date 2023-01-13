#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "tcp_ip.h"

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

#define FTP_CMD_MAXLEN (RAW_SEND_MAX - 50)
static char ftp_cmd[RAW_SEND_MAX - 40];
static volatile sig_atomic_t ftp_cmd_len = 0;
static volatile sig_atomic_t ftp_input_stopped = 0;
static bool ftp_file_end = false;

static void ftp_close_file(void) {
  if (tcp_output_file[1] == NULL) {
    return;
  }
  fclose(tcp_output_file[1]);
  tcp_output_file[1] = NULL;
}

static void ftp_handle_reply(const size_t reply_len) {
  if (reply_len < 3) {
    return;
  }
  char *reply_payload = raw_recv_payload + raw_recv_len - reply_len;
  reply_payload[reply_len] = '\0';
  if (memcmp(reply_payload, "227", 3) == 0) {
    char *left_paren = strchr(reply_payload, '(');
    if (left_paren == NULL) {
      return;
    }
    char *comma1 = strchr(left_paren + 1, ',');
    if (comma1 == NULL) {
      return;
    }
    char *comma2 = strchr(comma1 + 1, ',');
    if (comma2 == NULL) {
      return;
    }
    char *comma3 = strchr(comma2 + 1, ',');
    if (comma3 == NULL) {
      return;
    }
    char *comma4 = strchr(comma3 + 1, ',');
    if (comma4 == NULL) {
      return;
    }
    char *comma5 = strchr(comma4 + 1, ',');
    if (comma5 == NULL) {
      return;
    }
    char *right_paren = strchr(comma5 + 1, ')');
    if (right_paren == NULL) {
      return;
    }
    *comma1 = '.';
    *comma2 = '.';
    *comma3 = '.';
    *comma4 = '\0';
    if (inet_pton(AF_INET, left_paren + 1, &addr_dest[1]) == 0) {
      fprintf(stderr, "Invalid IP address: %s\n", left_paren + 1);
      return;
    }
    port_dest[1] = (atoi(comma4 + 1) << 8) + atoi(comma5 + 1);
    tcp_prepare_syn(1);
  }
}

static void *ftp_input_loop(void *args) {
  bool past_cmd = false;
  const char *cmd = NULL;
  size_t cmd_len = 0;
  for (;;) {
    while (ftp_cmd_len > 0) {
      sleep_ns(1000000);
    }
    const int c = getchar();
    if (c == EOF) {
      ftp_input_stopped = 1;
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
        if (cmd_len < 2 || ftp_cmd[cmd_len - 2] != '\r') {
          ftp_cmd[cmd_len - 1] = '\r';
          ftp_cmd[cmd_len++] = '\n';
        }
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
  if (inet_pton(AF_INET, argv[2], &addr_dest[0]) == 0) {
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

  srand(time_ns());
  port_host[0] = rand();
  port_host[1] = rand();

  pthread_t ftp_input_thread;
  pthread_create(&ftp_input_thread, NULL, ftp_input_loop, NULL);

  while (tcp_state[0] != TCP_CLOSE || tcp_state[1] != TCP_CLOSE ||
         !ftp_input_stopped) {
    if (!ftp_input_stopped && tcp_state[0] == TCP_CLOSE) {
      tcp_prepare_syn(0);
    } else if (ftp_input_stopped && tcp_state[0] == TCP_ESTABLISHED) {
      tcp_prepare_fin(0);
    }
    for (int d = 1; d >= 0; d--) {
      if (tcp_state[d] == TCP_CLOSE) {
        continue;
      }
      if (raw_send_len[d] > 0 && tcp_need_retry(d)) {
        struct sockaddr_in saddr_dest = {
            .sin_family = AF_INET,
            .sin_addr = ip_send_hdr_p[d]->daddr,
            .sin_port = 0,
        };
        sendto(ip_send_fd, raw_send_payload[d], raw_send_len[d], 0,
               (struct sockaddr *)&saddr_dest, sizeof(saddr_dest));
        tcp_timeout[d] = time_ns() + 2000000000;
        if (!tcp_need_ack(d)) {
          raw_send_len[d] = 0;
        }
        break;
      } else if (raw_send_len[d] == 0 && tcp_state[d] == TCP_CLOSE_WAIT) {
        tcp_prepare_fin(d);
      } else if (tcp_state[d] == TCP_TIME_WAIT && time_ns() > tcp_timeout[d]) {
        tcp_close(d);
      }
    }
    if (raw_send_len[0] == 0 && raw_send_len[1] == 0 &&
        tcp_state[0] == TCP_ESTABLISHED && ftp_cmd_len > 0) {
      if (ftp_cmd[0] == 'L') {
        ftp_close_file();
      } else if (ftp_cmd[0] == 'R') {
        ftp_close_file();
        if (ftp_cmd_len > 5 + 2) {
          ftp_cmd[ftp_cmd_len - 2] = '\0';
          tcp_output_file[1] = fopen(ftp_cmd + 5, "w");
          ftp_cmd[ftp_cmd_len - 2] = '\r';
        }
      }
      tcp_prepare_data(0, ftp_cmd, ftp_cmd_len);
    }
    sleep_ns(1000000);
    ssize_t recv_len =
        recvfrom(ip_recv_fd, S_LEN(raw_recv_payload), MSG_DONTWAIT, NULL, NULL);
    if (recv_len < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror(NULL);
      }
    } else if (ip_recv_hdr_p->ihl == 5) {
      raw_recv_len = recv_len;
      for (int d = 1; d >= 0; d--) {
        if (tcp_state[d] != TCP_CLOSE &&
            ntohs(tcp_recv_hdr_p->th_sport) == port_dest[d] &&
            ip_recv_hdr_p->saddr == addr_dest[d].s_addr &&
            ntohs(tcp_recv_hdr_p->th_dport) == port_host[d] &&
            ip_recv_hdr_p->daddr == addr_host.s_addr) {
          ssize_t tcp_recv_len = tcp_handle_recv(d);
          if (d == 0 && tcp_recv_len >= 0) {
            ftp_handle_reply(tcp_recv_len);
            if (!tcp_interrupted[d]) {
              ftp_cmd_len = 0;
            }
          }
          tcp_interrupted[d] = false;
          break;
        }
      }
    }
  }

  pthread_join(ftp_input_thread, NULL);

  return 0;
}
