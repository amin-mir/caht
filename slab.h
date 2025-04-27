#ifndef SLAB_H
#define SLAB_H

#include <stddef.h>

typedef struct {
	size_t buf_cap;
	size_t cap;
	size_t len;
	char **buffers;
} Slab;

void slab_init_cap(Slab *s, size_t buf_cap, size_t cap);
void slab_init(Slab *s, size_t buf_cap);
void slab_deinit(Slab *s);

/* Guarantees to return a buffer or exits the program if malloc fails. */
char *slab_get(Slab *s);

size_t slab_buf_cap(Slab *s);
void slab_put(Slab *s, char *buf);

#endif
