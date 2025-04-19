#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "../slab.h"

Test(slab, operations) {
	size_t buf_len = 1024;
	struct slab s;
	slab_init_cap(&s, buf_len, 2);

	// slab should have two free buffers so get both of them.
	size_t i;
	char *bufs[6];
	for (i = 0; i < 2; i++) {
		bufs[i] = slab_get(&s);
		cr_assert(bufs[i] != NULL);
	}

	// then get three more, slab should malloc separately for each.
	for (i = 2; i < 5; i++) {
		bufs[i] = slab_get(&s);
		cr_assert(bufs[i] != NULL);
	}

	// then return all to the slab. On the third put, slab will grow
	// to a len of 4.
	for (i = 0; i < 5; i++) {
		slab_put(&s, bufs[i]);
	}
	cr_assert(eq(sz, s.len, 5));
	cr_assert(eq(sz, s.cap, 8));

	// get six slabs and return all of them. cap should remain 8 and len should be 6.
	for (i = 0; i < 6; i++) {
		bufs[i] = slab_get(&s);
		cr_assert(bufs[i] != NULL);
	}
	for (i = 0; i < 6; i++) {
		slab_put(&s, bufs[i]);
	}
	cr_assert(eq(sz, s.len, 6));
	cr_assert(eq(sz, s.cap, 8));
}
