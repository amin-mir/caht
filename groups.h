#ifndef GROUPS_H
#define GROUPS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "cid_set.h"

struct grp {
	uint64_t gid;
	struct cid_set client_ids;
	struct grp *next;
};

struct groups {
	size_t size;
	struct grp *buckets[];
};

struct groups *groups_create(size_t capacity);
size_t groups_size(struct groups *g);

/**
 * Will return false if the entry could not be inserted into the group because
 * malloc failed. The value of `errno` indicates the error.
 */
bool groups_insert(struct groups *g, uint64_t gid, uint64_t cid);

/**
 * given a group id, points the provided client_ids pointer to 
 * the array holding the group members.
 */
bool groups_get(struct groups *g, uint64_t gid, struct cid_iter *iter);

#endif
