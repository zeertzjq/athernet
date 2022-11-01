#ifndef ATNET_PHYSICAL_H
#define ATNET_PHYSICAL_H

#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>

extern int volume;
extern bool has_ack;
extern sig_atomic_t receive_stopped;

void phy_init(void);
void transmit_frame(const bool *bits);
void transmit_ack(void);
void *receive_loop(void *args);
void receive_frame(bool *bits);
bool receive_ack(suseconds_t timeout);

#endif // ATNET_PHYSICAL_H
