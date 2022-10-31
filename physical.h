#ifndef ATNET_PHYSICAL_H
#define ATNET_PHYSICAL_H

#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>

extern int carrier_freq;
extern int volume;
extern sig_atomic_t capture_stopped;

void *capture_loop(void *args);
void phy_init(void);
void transmit_frame(const bool *bits);
void receive_frame(bool *bits, suseconds_t *timeout);

#endif // ATNET_PHYSICAL_H
