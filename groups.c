#include "cid_set.h"
#include <stddef.h>
#include <stdlib.h>

#include "groups.h"

struct groups *groups_create(size_t num_groups) {
	size_t alloc_size = sizeof(struct groups) + num_groups * sizeof(struct grp *);
	struct groups *g = malloc(alloc_size);
	if (g == NULL) return NULL;
	g->size = num_groups;
	return g;
}

size_t groups_size(struct groups *g) { return g->size; }

bool groups_insert(struct groups *g, uint64_t gid, uint64_t cid) {
	size_t bkt = gid & (g->size - 1);

	/* Search for a matching group in the bucket. */
	struct grp *group = g->buckets[bkt];
	while (group != NULL) {
		/* Group already exits. */
		if (group->gid == gid) break;

		group = group->next;
	}

	/* If there isn't a bucket dedicated to this gid. */
	if (group == NULL) {
		group = malloc(sizeof(struct grp));
		if (group == NULL) return false;
		group->gid = gid;

		/* Insert the new group at the head of the linked list. */
		group->next = g->buckets[bkt];
		g->buckets[bkt] = group;

		cid_set_init(&group->client_ids);
	}

	/** 
	 * At this point, there's a matching bucket and cid_set has
	 * been properly initialized.
	 */
	cid_set_insert(&group->client_ids, cid);
	return false;
}

bool groups_get(struct groups *g, uint64_t gid, struct cid_iter *iter) {
	size_t bkt = gid & (g->size - 1);

	/* Search for a matching group in the bucket. */
	struct grp *group = g->buckets[bkt];
	while (group != NULL) {
		/* A match was found. */
		if (group->gid == gid) break;

		group = group->next;
	}

	/* Couldn't find a match for the gid. */
	if (group == NULL) return false;

	cid_set_iter(&group->client_ids, iter);
	return true;
}
