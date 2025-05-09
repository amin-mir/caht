#include <stdlib.h>
#include <stdio.h>

#include "slab.h"
#include "utils.h"

#define SLAB_INIT_CAP 64

static inline BufRef *malloc_buf_ref(Slab *s) {
	return must_malloc(sizeof(BufRef) + s->buf_cap, "malloc_buf_ref");
}

void slab_init_cap(Slab *s, size_t buf_cap, size_t slab_cap) {
	s->cap = slab_cap;
	s->len = slab_cap;
	s->buf_cap = buf_cap;
	s->buf_refs = must_malloc(slab_cap * sizeof(BufRef *), "slab_init malloc buf_refs");

	for (size_t i = 0; i < slab_cap; i++) {
		s->buf_refs[i] = malloc_buf_ref(s);
	}
}

void slab_init(Slab *s, size_t buf_cap) {
	slab_init_cap(s, buf_cap, SLAB_INIT_CAP);
}

void slab_deinit(Slab *s) {
	/**
	 * The slab itself is managed by the caller of this function, so we don't
	 * free its memory here.
	 */
	for (size_t i = 0; i < s->len; i++) {
		free(s->buf_refs[i]);
	}
	free(s->buf_refs);
}

BufRef *slab_acquire(Slab *s, size_t ref) {
	BufRef *bref;

	/* Try to get a released BufRef or allocate a new one. */
	if (s->len > 0) {
		bref = s->buf_refs[s->len-1];
		s->len--;
	} else {
		bref = malloc_buf_ref(s);
	}

	bref->ref = ref;
	return bref;
}

size_t slab_buf_cap(Slab *s) {
	return s->buf_cap;
}

void slab_release(Slab *s, BufRef *bref) {
	bref->ref -= 1;
	if (bref->ref != 0) return;

	if (s->cap == s->len) {
		s->cap *= 2;
		s->buf_refs = must_realloc(
			s->buf_refs, 
			s->cap * sizeof(BufRef *),
			"slab_release realloc buffers"
		);
	}

	s->buf_refs[s->len] = bref;
	s->len++;
}
