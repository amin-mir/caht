#include "utils.h"
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>

void fatal_error(const char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

void *must_malloc(size_t size, const char *msg) {
	void *ptr = malloc(size);
	if (ptr == NULL) fatal_error(msg);
	return ptr;
}

void *must_calloc(size_t n, size_t size, const char *msg) {
	void *ptr = calloc(n, size);
	if (ptr == NULL) fatal_error(msg);
	return ptr;
}

void *must_realloc(void *ptr, size_t size, const char *msg) {
	ptr = realloc(ptr, size);
	if (ptr == NULL) fatal_error(msg);
	return ptr;
}

void must_close(int fd, const char *msg) {
	if (close(fd) < 0) fatal_error(msg);
}

void must_shutdown(int fd, const char *msg) {
	if (shutdown(fd, SHUT_RDWR) < 0) fatal_error(msg);
}

int read_int_from_buffer(const char *buf) {
	int network_value;
	memcpy(&network_value, buf, sizeof(int));
	return ntohl(network_value);
}

void write_int_to_buffer(char *buf, int value) {
	int network_value = htonl(value);
	memcpy(buf, &network_value, sizeof(int));
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
uint64_t htonll(uint64_t val) {
    return ((uint64_t)htonl(val >> 32)) | ((uint64_t)htonl(val & 0xFFFFFFFF) << 32);
}
uint64_t ntohll(uint64_t val) {
    return ((uint64_t)ntohl(val >> 32)) | ((uint64_t)ntohl(val & 0xFFFFFFFF) << 32);
}
#else
uint64_t htonll(uint64_t val) { return val; }
uint64_t ntohll(uint64_t val) { return val; }
#endif

bool is_prime(size_t n) {
	if (n <= 1) return false;
	if (n == 2) return true;
	if (n % 2 == 0) return false;
	for (size_t i = 3; i * i <= n; i += 2) {
		if (n % i == 0) {
			return false;
		}
	}
	return true;
}

size_t closest_prime(size_t n) {
	while (!is_prime(n)) {
		n++;
	}
	return n;
}

void set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) fatal_error("fcntl(F_GETFL)");

	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		fatal_error("fcntl(F_SETFL, O_NONBLOCK)");
}

