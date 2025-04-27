#ifndef CLIENT_ID_SET_H
#define CLIENT_ID_SET_H

/**
 * Implementation of a HashSet with keys of type uint64_t
 * with open addressing and linear probing.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct cid_set {
	size_t len;
	/* cap must remain a multiple of 2. */
	size_t cap;
	uint64_t *ids;
};

struct cid_iter {
	size_t idx;
	size_t len;
	uint64_t *ids;
};

/* Returns NULL in case of error in the allocatin of struct cidset. */
void cid_set_init(struct cid_set *set);

bool cid_set_exists(struct cid_set *set, uint64_t id);

/* id cannot be UINT64_MAX. */
void cid_set_insert(struct cid_set *set, uint64_t id);

void cid_set_iter(struct cid_set *set, struct cid_iter *iter);

/** 
 * Tries to fill len number of client ids into the batch array. Returns the
 * number of client ids filled.
 */
size_t cid_iter_next_batch(struct cid_iter *iter, size_t len, uint64_t batch[len]);

#endif
