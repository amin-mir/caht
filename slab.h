#ifndef SLAB_H
#define SLAB_H

/* Slab API with reference counting. */

#include <stddef.h>

typedef struct {
	/**
	 * Count of references to this buffer. Must not be modified externally.
	 * BufRef is only returned to the slab once ref reaches 0.
	 */
	size_t ref;
	char buf[];
} BufRef;

typedef struct {
	/* cap & len for the buf_refs array. buf_refs is growable. */
	size_t cap;
	size_t len;

	/* Cap for the buffers returned by this slab. This buffers will all have a fixed capacity */
	size_t buf_cap;
	BufRef **buf_refs;
} Slab;

void slab_init_cap(Slab *s, size_t buf_cap, size_t cap);
void slab_init(Slab *s, size_t buf_cap);
void slab_deinit(Slab *s);

/* Returns a buffer or exits the program if memory allocation fails. */
BufRef *slab_acquire(Slab *s, size_t ref);

size_t slab_buf_cap(Slab *s);

/**
 * Decrements the ref count and only releases the bref if ref reaches 0.
 * Slab could potentially grow if there isn't enough room to accomodate the bref.
 * Growth only happens if BufRef ref reaches 0.
 */
void slab_release(Slab *s, BufRef *bref);

#endif
