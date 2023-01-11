#ifndef ATNET_COMMON_H
#define ATNET_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define RATE 48000
#define LEN(s) (sizeof(s) - 1)
#define S_LEN(s) (s), LEN(s)

static inline uint8_t compose_u8(const bool *const bits) {
  uint8_t num = 0;
  for (int i = 0; i < 8; i++) {
    num |= bits[i] << i;
  }
  return num;
}

static inline void decompose_u8(const uint8_t num, bool *const bits) {
  for (int i = 0; i < 8; i++) {
    bits[i] = num & (1 << i);
  }
}

static inline uint16_t compose_u16(const bool *const bits) {
  uint16_t num = 0;
  for (int i = 0; i < 16; i++) {
    num |= bits[i] << i;
  }
  return num;
}

static inline void decompose_u16(const uint16_t num, bool *const bits) {
  for (int i = 0; i < 16; i++) {
    bits[i] = num & (1 << i);
  }
}

static inline uint32_t compose_u32(const bool *const bits) {
  uint32_t num = 0;
  for (int i = 0; i < 32; i++) {
    num |= bits[i] << i;
  }
  return num;
}

static inline void decompose_u32(const uint32_t num, bool *const bits) {
  for (int i = 0; i < 32; i++) {
    bits[i] = num & (1 << i);
  }
}

static inline uint16_t inet_checksum(const uint8_t *bytes, size_t count) {
  uint16_t res = 0;
  for (size_t i = 0; i < count; i += 2) {
    uint32_t tmp = res + (i == count - 1 ? bytes[i] : *(uint16_t *)&bytes[i]);
    res = (tmp >> 16) + (tmp & 0xFFFF);
  }
  return ~res;
}

static inline int64_t time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

static inline void sleep_ns(long ns) {
  const struct timespec ts = {
      .tv_nsec = ns,
  };
  nanosleep(&ts, NULL);
}

#endif // ATNET_COMMON_H
