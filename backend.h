#ifndef ATNET_BACKEND_H
#define ATNET_BACKEND_H

#include <stddef.h>
#include <stdint.h>

void capture_start(void);
void capture_stop(void);
void capture_read(int16_t *buf, size_t len);

void playback_start(void);
void playback_stop(void);
void playback_write(int16_t *buf, size_t len);

#endif // ATNET_BACKEND_H
