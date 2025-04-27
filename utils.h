#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdint.h>

#define ROUND_UP_POW_2(x) \
	(((x) < 1) ? 1 : ({ \
		size_t _v = (x) - 1; \
		_v |= _v >> 1; \
		_v |= _v >> 2; \
		_v |= _v >> 4; \
		_v |= _ \
		_v |= _v >> 16; \
		_v |= _v >> 32; \
		_v + 1; \
	}))

void fatal_error(const char *msg);
void *must_malloc(size_t size, const char *msg);
void *must_calloc(size_t n, size_t size, const char *msg);
void *must_realloc(void *ptr, size_t size, const char *msg);
void must_close(int fd, const char *msg);
void must_shutdown(int fd, const char *msg);
int read_int_from_buffer(const char *buf);
void write_int_to_buffer(char *buf, int value);
uint64_t htonll(uint64_t val);
uint64_t ntohll(uint64_t val);
size_t closest_prime(size_t n);
void set_nonblocking(int fd);

#endif
