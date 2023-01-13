#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RAW_SEND_MAX 200
#define RAW_RECV_MAX 240
extern int ip_send_fd;
extern int ip_recv_fd;
extern struct in_addr addr_host;
extern struct in_addr addr_dest[2];
extern uint16_t port_host[2];
extern uint16_t port_dest[2];
extern char raw_send_payload[2][RAW_SEND_MAX];
extern char raw_recv_payload[RAW_RECV_MAX];
extern size_t raw_send_len[2];
extern size_t raw_recv_len;
extern struct iphdr *const ip_send_hdr_p[2];
extern struct iphdr *const ip_recv_hdr_p;
extern char *const ip_send_payload[2];
extern char *const ip_recv_payload;
extern struct tcphdr *const tcp_send_hdr_p[2];
extern struct tcphdr *const tcp_recv_hdr_p;
extern char *const tcp_send_payload[2];
extern FILE *tcp_output_file[2];
extern int64_t tcp_timeout[2];
extern bool tcp_interrupted[2];
extern int tcp_state[2];

void tcp_fill_checksum(bool d);
void tcp_prepare_syn(bool d);
void tcp_prepare_data(bool d, const char *data, size_t len);
void tcp_prepare_fin(bool d);
void tcp_close(bool d);
bool tcp_need_ack(bool d);
bool tcp_need_retry(bool d);
bool tcp_recv_check(bool d);
ssize_t tcp_handle_recv(bool d);
