#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct in_addr addr_host;
static struct in_addr addr_dest;

static int tcp_states[2] = {TCP_CLOSE, TCP_CLOSE};

static const char *const ftp_cmds[] = {
    "USER", "PASS", "PWD", "CWD", "PASV", "LIST", "RETR",
};
static size_t cmd_matched[ARRAY_SIZE(ftp_cmds)][4] = {0};

static void ftp_parse_reset(void) {
  memset(cmd_matched, 0, sizeof(cmd_matched));
}

static void ftp_parse_add(int c) {
  if (c >= 'a' && c <= 'z') {
    c += 'A' - 'a';
  } else if (c < 'A' || c > 'Z') {
    return;
  }
  for (size_t cmd_idx = 0; cmd_idx < ARRAY_SIZE(ftp_cmds); cmd_idx++) {
    const char *const cmd = ftp_cmds[cmd_idx];
    const size_t cmd_len = strlen(cmd);
    size_t *const matched_old = cmd_matched[cmd_idx];
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
  for (size_t cmd_idx = 0; cmd_idx < ARRAY_SIZE(ftp_cmds); cmd_idx++) {
    const char *const cmd = ftp_cmds[cmd_idx];
    const size_t len = cmd_matched[cmd_idx][strlen(cmd) - 1];
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
  for (size_t cmd_idx = 0; cmd_idx < ARRAY_SIZE(ftp_cmds); cmd_idx++) {
    const char *const cmd = ftp_cmds[cmd_idx];
    const size_t len = cmd_matched[cmd_idx][strlen(cmd) - 1];
    if (len == max_matched) {
      printf(" %s", ftp_cmds[cmd_idx]);
    }
  }
  puts("");
  return NULL;
}

static size_t ftp_next_cmd(char *const buf, const size_t buf_len) {
  bool past_cmd = false;
  const char *cmd = NULL;
  size_t new_len = 0;
  for (;;) {
    const int c = getchar();
    if (c == EOF) {
      return 0;
    }
    if (!past_cmd) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        past_cmd = true;
        cmd = ftp_parse_get();
        ftp_parse_reset();
        if (cmd != NULL) {
          strncpy(buf, cmd, buf_len);
          new_len = strlen(cmd);
          if (new_len > buf_len) {
            new_len = buf_len;
          }
        }
      } else {
        ftp_parse_add(c);
      }
    }
    if (cmd != NULL && new_len < buf_len) {
      buf[new_len++] = c;
    }
    if (c == '\n') {
      if (cmd != NULL) {
        return new_len;
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

  return 0;
}
