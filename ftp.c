#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "ftp.h"
#include "tcp_ip.h"

volatile sig_atomic_t ftp_input_stopped = 0;

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

#define FTP_CMD_MAXLEN (RAW_PAYLOAD_MAX - 50)
static char ftp_cmd[RAW_PAYLOAD_MAX - 40];
static volatile sig_atomic_t ftp_cmd_len = 0;

void *ftp_input_loop(void *args) {
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

static void ftp_close_file(void) {
  if (tcp_output_file[1] == NULL) {
    return;
  }
  fclose(tcp_output_file[1]);
  tcp_output_file[1] = NULL;
}

void ftp_prepare_cmd(void) {
  if (tcp_state[0] != TCP_ESTABLISHED || ftp_cmd_len == 0) {
    return;
  }
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

static void ftp_handle_227(char *reply_payload) {
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

void ftp_handle_reply(const size_t reply_len) {
  if (reply_len >= 3) {
    char *reply_payload = raw_recv_payload + raw_recv_len - reply_len;
    reply_payload[reply_len] = '\0';
    if (memcmp(reply_payload, "227", 3) == 0) {
      ftp_handle_227(reply_payload);
    }
  }
  ftp_cmd_len = 0;
}
