#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "tcp_ip.h"

int ip_send_fd = -1;
int ip_recv_fd = -1;
struct in_addr addr_host;
struct in_addr addr_dest[2];
uint16_t port_host[2] = {0, 0};
uint16_t port_dest[2] = {21, 0};
char raw_send_payload[2][RAW_PAYLOAD_MAX];
char raw_recv_payload[RAW_PAYLOAD_MAX];
size_t raw_send_len[2] = {0, 0};
size_t raw_recv_len = 0;
struct iphdr *const ip_send_hdr_p[2] = {
    (struct iphdr *)raw_send_payload[0],
    (struct iphdr *)raw_send_payload[1],
};
struct iphdr *const ip_recv_hdr_p = (struct iphdr *)raw_recv_payload;
char *const ip_send_payload[2] = {
    raw_send_payload[0] + sizeof(struct iphdr),
    raw_send_payload[1] + sizeof(struct iphdr),
};
char *const ip_recv_payload = raw_recv_payload + sizeof(struct iphdr);
struct tcphdr *const tcp_send_hdr_p[2] = {
    (struct tcphdr *)(raw_send_payload[0] + sizeof(struct iphdr)),
    (struct tcphdr *)(raw_send_payload[1] + sizeof(struct iphdr)),
};
struct tcphdr *const tcp_recv_hdr_p =
    (struct tcphdr *)(raw_recv_payload + sizeof(struct iphdr));
char *const tcp_send_payload[2] = {
    raw_send_payload[0] + sizeof(struct iphdr) + sizeof(struct tcphdr),
    raw_send_payload[1] + sizeof(struct iphdr) + sizeof(struct tcphdr),
};
FILE *tcp_output_file[2] = {NULL, NULL};
int64_t tcp_timeout[2] = {0, 0};
bool tcp_interrupted[2] = {false, false};
int tcp_state[2] = {TCP_CLOSE, TCP_CLOSE};

static tcp_seq tcp_want_seq[2] = {0, 0};

void tcp_fill_checksum(char *raw_payload, const size_t raw_len) {
  struct iphdr *const ip_hdr_p = (struct iphdr *)raw_payload;
  char *const ip_payload = raw_payload + sizeof(struct iphdr);
  struct tcphdr *const tcp_hdr_p = (struct tcphdr *)ip_payload;
  tcp_hdr_p->th_sum = 0;
  const uint16_t tcp_len = raw_len - sizeof(struct iphdr);
  struct {
    uint32_t saddr, daddr;
    uint8_t zeros, protocol;
    uint16_t tcp_len;
  } pseudo_hdr = {
      .saddr = ip_hdr_p->saddr,
      .daddr = ip_hdr_p->daddr,
      .protocol = IPPROTO_TCP,
      .tcp_len = htons(tcp_len),
  };
  uint32_t sum = 0;
  for (size_t i = 0; i < sizeof(pseudo_hdr) / 2; i++) {
    sum += ((uint16_t *)&pseudo_hdr)[i];
  }
  for (size_t i = 0; i < tcp_len / 2; i++) {
    sum += ((uint16_t *)ip_payload)[i];
  }
  if (tcp_len & 1) {
    sum += ((uint8_t *)ip_payload)[tcp_len - 1];
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  tcp_hdr_p->th_sum = ~sum;
}

void tcp_prepare_syn(const bool d) {
  *ip_send_hdr_p[d] = (struct iphdr){
      .ihl = 5,
      .version = 4,
      .ttl = 255,
      .protocol = IPPROTO_TCP,
      .saddr = addr_host.s_addr,
      .daddr = addr_dest[d].s_addr,
  };
  *tcp_send_hdr_p[d] = (struct tcphdr){
      .th_sport = htons(port_host[d]),
      .th_dport = htons(port_dest[d]),
      .th_seq = htonl(rand()),
      .th_off = 5,
      .th_flags = TH_SYN,
      .th_win = htons(RAW_PAYLOAD_MAX - 80),
  };
  raw_send_len[d] = sizeof(struct iphdr) + sizeof(struct tcphdr);
  tcp_fill_checksum(raw_send_payload[d], raw_send_len[d]);
  tcp_timeout[d] = 0;
  tcp_state[d] = TCP_SYN_SENT;
}

void tcp_prepare_data(const bool d, const char *const data, const size_t len) {
  memcpy(tcp_send_payload[d], data, len);
  tcp_send_hdr_p[d]->th_flags = TH_ACK;
  raw_send_len[d] = sizeof(struct iphdr) + sizeof(struct tcphdr) + len;
  tcp_fill_checksum(raw_send_payload[d], raw_send_len[d]);
  tcp_timeout[d] = 0;
}

void tcp_prepare_fin(const bool d) {
  tcp_send_hdr_p[d]->th_flags = TH_ACK | TH_FIN;
  raw_send_len[d] = sizeof(struct iphdr) + sizeof(struct tcphdr);
  tcp_fill_checksum(raw_send_payload[d], raw_send_len[d]);
  tcp_timeout[d] = 0;
  if (tcp_state[d] == TCP_ESTABLISHED) {
    tcp_state[d] = TCP_FIN_WAIT1;
  } else if (tcp_state[d] == TCP_CLOSE_WAIT) {
    tcp_state[d] = TCP_LAST_ACK;
  }
}

void tcp_close(const bool d) {
  if (tcp_output_file[d] != NULL) {
    fclose(tcp_output_file[d]);
  }
  tcp_output_file[d] = NULL;
  raw_send_len[d] = 0;
  tcp_state[d] = TCP_CLOSE;
}

static bool tcp_need_ack(const bool d) {
  return raw_send_len[d] > sizeof(struct iphdr) + sizeof(struct tcphdr) ||
         tcp_send_hdr_p[d]->th_flags != TH_ACK;
}

bool tcp_need_retry(const bool d) {
  return tcp_timeout[d] == 0 || tcp_need_ack(d) && time_ns() > tcp_timeout[d];
}

void tcp_after_send(const bool d) {
  tcp_timeout[d] = time_ns() + 2000000000;
  if (!tcp_need_ack(d)) {
    raw_send_len[d] = 0;
  }
}

bool tcp_recv_check(const bool d) {
  return tcp_state[d] != TCP_CLOSE &&
         ntohs(tcp_recv_hdr_p->th_sport) == port_dest[d] &&
         ip_recv_hdr_p->saddr == addr_dest[d].s_addr &&
         ntohs(tcp_recv_hdr_p->th_dport) == port_host[d] &&
         ip_recv_hdr_p->daddr == addr_host.s_addr;
}

ssize_t tcp_handle_recv(const bool d) {
  if (tcp_recv_hdr_p->th_flags & TH_RST) {
    tcp_close(d);
    return -1;
  }
  if (!(tcp_recv_hdr_p->th_flags & TH_ACK)) {
    return -1;
  }
  const size_t tcp_recv_hdr_len = (tcp_recv_hdr_p->th_off << 2);
  const char *tcp_recv_payload = ip_recv_payload + tcp_recv_hdr_len;
  const size_t tcp_recv_len =
      raw_recv_len - sizeof(struct iphdr) - tcp_recv_hdr_len;
  bool need_reply = true;
  uint16_t seq_extra = 0;
  if (tcp_recv_hdr_p->th_flags & TH_SYN) {
    if (tcp_state[d] == TCP_SYN_SENT) {
      tcp_state[d] = TCP_ESTABLISHED;
      raw_send_len[d] = 0;
    } else if (tcp_state[d] != TCP_ESTABLISHED) {
      return -1;
    }
    seq_extra = 1;
  } else if (tcp_recv_hdr_p->th_seq == htonl(tcp_want_seq[d])) {
    if (tcp_state[d] == TCP_FIN_WAIT1) {
      tcp_state[d] = TCP_FIN_WAIT2;
    } else if (tcp_state[d] == TCP_CLOSING) {
      tcp_state[d] = TCP_TIME_WAIT;
    } else if (tcp_state[d] == TCP_LAST_ACK) {
      tcp_close(d);
      return -1;
    }
    if (tcp_recv_len > 0) {
      FILE *output = tcp_output_file[d] != NULL ? tcp_output_file[d] : stdout;
      fwrite(tcp_recv_payload, 1, tcp_recv_len, output);
      fflush(output);
    } else {
      need_reply = false;
    }
    raw_send_len[d] = 0;
  } else if (tcp_recv_len == 0) {
    need_reply = false;
  }
  if (tcp_recv_hdr_p->th_flags & TH_FIN) {
    if (tcp_state[d] == TCP_ESTABLISHED) {
      tcp_state[d] = TCP_CLOSE_WAIT;
    } else if (tcp_state[d] == TCP_FIN_WAIT2) {
      tcp_state[d] = TCP_TIME_WAIT;
    } else if (tcp_state[d] == TCP_FIN_WAIT1) {
      if (tcp_recv_hdr_p->th_seq == htonl(tcp_want_seq[d])) {
        tcp_state[d] = TCP_TIME_WAIT;
      } else {
        tcp_state[d] = TCP_CLOSING;
      }
    }
    need_reply = true;
    seq_extra = 1;
  }
  tcp_send_hdr_p[d]->th_seq = tcp_recv_hdr_p->th_ack;
  if (need_reply) {
    if (raw_send_len[d] > 0) {
      tcp_interrupted[d] = true;
    }
    tcp_want_seq[d] = ntohl(tcp_recv_hdr_p->th_seq) + tcp_recv_len + seq_extra;
    tcp_send_hdr_p[d]->th_ack = htonl(tcp_want_seq[d]);
    tcp_send_hdr_p[d]->th_flags = TH_ACK;
    raw_send_len[d] = sizeof(struct iphdr) + sizeof(struct tcphdr);
    tcp_fill_checksum(raw_send_payload[d], raw_send_len[d]);
    tcp_timeout[d] = 0;
  }
  return tcp_recv_len;
}
