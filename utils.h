#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>

void fatal_error(const char *msg);
void *must_malloc(size_t size);
void must_close(int fd);
int read_int_from_buffer(const char *buf);
void write_int_to_buffer(char *buf, int value);

#endif
