#include <stdlib.h>
#include <stdio.h>

#include "slab.h"

#define SLAB_INIT_CAP 64

void slab_init_cap(struct slab *s, size_t buf_len, size_t cap) {
	s->cap = cap;
	s->len = cap;
	s->buf_len = buf_len;
	s->buffers = malloc(cap * sizeof(char *));
	if (s->buffers == NULL) {
		perror("slab_init: malloc");
		exit(EXIT_FAILURE);
	} 

	for (size_t i = 0; i < s->cap; i++) {
		char *buf = malloc(buf_len);
		if (buf == NULL) {
			perror("slab_init buffer: malloc");
			exit(EXIT_FAILURE);
		} 
		s->buffers[i] = buf;
	}
}

void slab_init(struct slab *s, size_t buf_len) {
	slab_init_cap(s, buf_len, SLAB_INIT_CAP);
}

void slab_deinit(struct slab *s) {
	for (size_t i = 0; i < s->cap; i++) {
		free(s->buffers[i]);
	}
	free(s->buffers);
}

char *slab_get(struct slab *s) {
	char *buf;
	if (s->len > 0) {
		buf = s->buffers[0];
		s->buffers[0] = s->buffers[s->len-1];
		s->len--;
		return buf;
	}

	buf = malloc(s->buf_len);
	if (buf == NULL) {
		perror("slab_get buffer: malloc");
		exit(EXIT_FAILURE);
	}
	return buf;
}

void slab_put(struct slab *s, char *buf) {
	if (s->cap == s->len) {
		size_t new_cap = s->cap * 2;
		s->buffers = realloc(s->buffers, new_cap * sizeof(char *));
		if (s->buffers == NULL) {
			perror("slab_put: realloc");
			exit(EXIT_FAILURE);
		}
		s->cap = new_cap;
	}

	s->buffers[s->len] = buf;
	s->len++;
}
