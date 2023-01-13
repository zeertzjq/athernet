#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "common.h"
#include "ftp.h"
#include "tcp_ip.h"

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
        tcp_state[0] == TCP_ESTABLISHED) {
      ftp_prepare_cmd();
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
