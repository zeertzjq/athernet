#ifndef ATNET_PHYSICAL_H
#define ATNET_PHYSICAL_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define PHY_PAYLOAD_FIXED 200
#define PHY_PAYLOAD_MAX 500

extern int volume;
extern bool has_ack;
extern volatile sig_atomic_t receive_stopped;

void phy_init(void);
void transmit_frame(const bool *bits, size_t len);
void *receive_loop(void *args);
void receive_frame(bool *bits, size_t len, suseconds_t *timeout);

#endif // ATNET_PHYSICAL_H
