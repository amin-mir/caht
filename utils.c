#include "utils.h"
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void fatal_error(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void *must_malloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL)
    fatal_error("malloc");
  return ptr;
}

void must_close(int fd) {
  if (close(fd) < 0)
    fatal_error("close fd");
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
