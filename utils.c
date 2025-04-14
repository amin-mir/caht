#include "utils.h"
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

void fatal_error(const char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

void *must_malloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr == NULL) fatal_error("malloc");
	return ptr;
}

void must_close(int fd) {
	if (close(fd) < 0) fatal_error("close fd");
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
