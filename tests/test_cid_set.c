#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "../cid_set.h"

int compare_cids(const void *a, const void *b) {
	uint64_t cid1 = *((uint64_t *)a);
	uint64_t cid2 = *((uint64_t *) b);
	if (cid1 < cid2) return -1;
	if (cid1 > cid2) return 1;
	return 0;
}

Test(cid_set, operations) {
	struct cid_set set;
	cid_set_init(&set);

	size_t old_cap = set.cap;
	cr_assert(set.len == 0);
	for (size_t i = 0; i < old_cap; i++) {
		cid_set_insert(&set, i);
	}

	cr_assert(eq(u64, set.len, old_cap));
	cr_assert(eq(u64, set.cap, 2 * old_cap));
	for (size_t i = 0; i < old_cap; i++) {
		cr_assert(cid_set_exists(&set, i));
	}

	cr_assert(not(cid_set_exists(&set, 1000)));

	struct cid_iter iter;
	cid_set_iter(&set, &iter);
	uint64_t batch[3];
	size_t fetched = 0;
	size_t num_iters = (set.len + 2) / 3;

	for (size_t i = 0; i < num_iters; i++) {
		fetched += cid_iter_next_batch(&iter, 3, batch);
	}
	cr_assert(eq(u64, fetched, set.len));

	cid_set_iter(&set, &iter);
	uint64_t big_batch[20];
	fetched = cid_iter_next_batch(&iter, 20, big_batch);
	cr_assert(eq(u64, fetched, set.len));
	qsort(big_batch, fetched, sizeof(uint64_t), compare_cids);
	for (size_t i = 0; i < set.len; i++) {
		cr_assert(eq(u64, big_batch[i], i));
	}
}
