#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>

#define ROUND_UP_POW_2(x)                                                      \
  (((x) < 1) ? 1 : ({                                                          \
    size_t _v = (x) - 1;                                                       \
    _v |= _v >> 1;                                                             \
    _v |= _v >> 2;                                                             \
    _v |= _v >> 4;                                                             \
    _v |= _v >> 8;                                                             \
    _v |= _v >> 16;                                                            \
    _v |= _v >> 32;                                                            \
    _v + 1;                                                                    \
  }))

void fatal_error(const char *msg);
void *must_malloc(size_t size);
void must_close(int fd);
int read_int_from_buffer(const char *buf);
void write_int_to_buffer(char *buf, int value);

#endif
