#ifndef ATNET_PHYSICAL_H
#define ATNET_PHYSICAL_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PHY_PAYLOAD_FIXED 200
#define PHY_PAYLOAD_MAX 1000

extern int volume;
extern bool has_ack;
extern bool noisy;
extern volatile sig_atomic_t phy_receive_stopped;
extern volatile sig_atomic_t phy_receiving_frame;

void phy_init(void);
void phy_transmit_frame(const bool *bits, size_t len);
void *phy_receive_loop(void *args);
size_t phy_receive_frame(bool *bits, size_t max_len, int64_t *timeout_ns);

#endif // ATNET_PHYSICAL_H
