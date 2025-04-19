#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "../groups.h"

Test(groups, operations) {
	size_t group_size = 1024;
	struct groups *g = groups_create(group_size);
	cr_assert(eq(u64, groups_size(g), group_size));

	// 4 clients in g0
	uint64_t g0 = 0;
	for (uint64_t cid = 0; cid < 4; cid++) {
		groups_insert(g, g0, cid);
	}

	// 4 clients in g1
	uint64_t g1 = 1;
	for (uint64_t cid = 0; cid < 4; cid++) {
		groups_insert(g, g1, cid);
	}

	// 10 clients in g1031
	// Should map to the same bucket as g0.
	uint64_t g2 = group_size;
	for (uint64_t cid = 0; cid < 10; cid++) {
		groups_insert(g, g2, cid);
	}

	uint64_t batch[20];
	struct cid_iter iter;
	cr_assert(not(groups_get(g, 100, &iter)));

	cr_assert(groups_get(g, g0, &iter));
	size_t n = cid_iter_next_batch(&iter, 20, batch);
	cr_assert(eq(u64, n, 4));

	cr_assert(groups_get(g, g1, &iter));
	n = cid_iter_next_batch(&iter, 20, batch);
	cr_assert(eq(u64, n, 4));

	cr_assert(groups_get(g, g2, &iter));
	n = cid_iter_next_batch(&iter, 20, batch);
	cr_assert(eq(u64, n, 10));
}
