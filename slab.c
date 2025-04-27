#include <stdlib.h>
#include <stdio.h>

#include "slab.h"
#include "utils.h"

#define SLAB_INIT_CAP 64

void slab_init_cap(Slab *s, size_t buf_cap, size_t cap) {
	s->cap = cap;
	s->len = cap;
	s->buf_cap = buf_cap;
	s->buffers = must_malloc(cap * sizeof(char *), "slab_init malloc buffers");

	for (size_t i = 0; i < s->cap; i++) {
		s->buffers[i] = must_malloc(buf_cap, "slab_init malloc buffers[i]");
	}
}

void slab_init(Slab *s, size_t buf_len) {
	slab_init_cap(s, buf_len, SLAB_INIT_CAP);
}

void slab_deinit(Slab *s) {
	/**
	 * The slab itself is managed by the caller of this function, so we don't
	 * free its memory here.
	 */
	for (size_t i = 0; i < s->len; i++) {
		free(s->buffers[i]);
	}
	free(s->buffers);
}

char *slab_get(Slab *s) {
	if (s->len > 0) {
		char *buf = s->buffers[s->len-1];
		s->len--;
		return buf;
	}

	return must_malloc(s->buf_cap, "slab_get malloc buffer");
}

size_t slab_buf_cap(Slab *s) {
	return s->buf_cap;
}

void slab_put(Slab *s, char *buf) {
	if (s->cap == s->len) {
		s->cap *= 2;
		s->buffers = must_realloc(
			s->buffers, 
			s->cap * sizeof(char *),
			"slab_put realloc buffers"
		);
	}

	s->buffers[s->len] = buf;
	s->len++;
}
