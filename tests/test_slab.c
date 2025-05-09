#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "../slab.h"

Test(slab, operations) {
	size_t buf_cap = 1024;
	Slab s;
	slab_init_cap(&s, buf_cap, 2);

	/* slab should have two free buffers so get both of them. */
	size_t i;
	BufRef *bufs[6];
	for (i = 0; i < 2; i++) {
		bufs[i] = slab_acquire(&s, 1);
		cr_assert(bufs[i] != NULL);
	}

	/* then get three more, slab should malloc separately for each. */
	for (i = 2; i < 5; i++) {
		bufs[i] = slab_acquire(&s, 1);
		cr_assert(bufs[i] != NULL);
	}

	/* Return all back to the slab. Slab will eventually grow to a cap of 8. */
	for (i = 0; i < 5; i++) {
		slab_release(&s, bufs[i]);
	}
	cr_assert(eq(sz, s.len, 5));
	cr_assert(eq(sz, s.cap, 8));

	/* Get six BufRefs and return all of them. cap should remain 8 and len should be 6. */
	for (i = 0; i < 6; i++) {
		bufs[i] = slab_acquire(&s, 1);
		cr_assert(bufs[i] != NULL);
	}
	for (i = 0; i < 6; i++) {
		slab_release(&s, bufs[i]);
	}
	cr_assert(eq(sz, s.len, 6));
	cr_assert(eq(sz, s.cap, 8));

	slab_deinit(&s);
}

Test(slab, ref_check) {
	size_t buf_cap = 1024;
	size_t slab_cap = 1;
	Slab s;
	slab_init_cap(&s, buf_cap, slab_cap);

	/* Acquire from slab to exhaust the available BufRefs. */
	BufRef *b_ref1 = slab_acquire(&s, 1);

	BufRef *b_ref3 = slab_acquire(&s, 3);

	/* Should return BufRef to slab and increment the len. */
	slab_release(&s, b_ref1);
	cr_assert(eq(sz, s.len, 1));
	cr_assert(eq(sz, s.cap, 1));

	/**
	 * slab_release has to be called 3 times for the BufRef to be returned.
	 * Slab should not grow unless ref is 0.
	 */
	slab_release(&s, b_ref3);
	cr_assert(eq(sz, b_ref3->ref, 2));
	cr_assert(eq(sz, s.len, 1));
	cr_assert(eq(sz, s.cap, 1));

	/**
	 * Release two more times so that ref becomes 0 and is put back into slab.
	 * Slab will need to grow to a cap of 2.
	 */
	slab_release(&s, b_ref3);
	slab_release(&s, b_ref3);
	cr_assert(eq(sz, s.len, 2));
	cr_assert(eq(sz, s.cap, 2));
}
