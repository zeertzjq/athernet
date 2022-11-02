#ifndef ATNET_COMMON_H
#define ATNET_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#define RATE 48000
#define LEN(s) (sizeof(s) - 1)
#define S_LEN(s) (s), LEN(s)

static inline uint8_t compose_byte(const bool *const bits) {
  uint8_t byte = 0;
  for (int i = 0; i < 8; i++) {
    byte |= bits[i] << i;
  }
  return byte;
}

static inline void decompose_byte(const uint8_t byte, bool *const bits) {
  for (int i = 0; i < 8; i++) {
    bits[i] = byte & (1 << i);
  }
}

#endif // ATNET_COMMON_H
