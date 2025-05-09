#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "../op_pool.h"

Test(op_pool, operations) {
	OpPool pool;
	op_pool_init(&pool);
	op_pool_deinit(&pool);

	size_t ops_cap = 8;
	size_t free_cap = 4;
	op_pool_init_with_cap(&pool, ops_cap, free_cap);

	Operation *op = op_pool_new_entry(&pool);
	/* Contract: client_fd is -1 and buf is NULL for the new entry. */
	cr_assert(eq(sz, op->pool_id, 0));
	cr_assert(eq(i32, op->client_fd, -1));
	cr_assert(zero(ptr, op->buf_ref));
	/* ops_next_idx advances by 1 and free_ops remains unchanged. */
	cr_assert(eq(sz, pool.ops_next_idx, 1));
	cr_assert(eq(sz, pool.free_len, 0));
	cr_assert(eq(sz, pool.free_cap, free_cap));

	/* Contract: client_fd is -1 and buf is NULL before return. */
	op_pool_return(&pool, op);
	cr_assert(eq(sz, pool.free_len, 1));

	op = op_pool_new_entry(&pool);
	/**
	 * Contract: client_fd is -1 and buf is NULL for the new entry.
	 * Reuses the previous Operation that was returned to the pool.
	 */
	cr_assert(eq(sz, op->pool_id, 0));
	cr_assert(eq(i32, op->client_fd, -1));
	cr_assert(zero(ptr, op->buf_ref));
	/* ops_next_idx advances by 1 and free_ops remains unchanged. */
	cr_assert(eq(sz, pool.ops_next_idx, 1));
	cr_assert(eq(sz, pool.free_len, 0));
	
	op_pool_deinit(&pool);
}

Test(op_pool, array_grow) {
	size_t ops_cap = 8;
	size_t free_cap = 4;
	OpPool pool;
	op_pool_init_with_cap(&pool, ops_cap, free_cap);

	size_t alloc_ops = 10;
	Operation *ops[10] = {NULL};
	for (size_t i = 0; i < alloc_ops; i++) {
		ops[i] = op_pool_new_entry(&pool);
	}

	cr_assert(eq(sz, pool.ops_next_idx, 10));
	cr_assert(eq(sz, pool.ops_cap, ops_cap * 2));

	for (size_t i = 0; i < alloc_ops; i++) {
		op_pool_return(&pool, ops[i]);
	}
	cr_assert(eq(sz, pool.free_cap, free_cap * 4));
	cr_assert(eq(sz, pool.free_len, 10));

	for (size_t i = 0; i < alloc_ops; i++) {
		ops[i] = op_pool_new_entry(&pool);
	}

	/**
	 * None of the backing storage grows as we'll only reuse Operations
	 * from the free list.
	 */
	cr_assert(eq(sz, pool.ops_next_idx, 10));
	cr_assert(eq(sz, pool.ops_cap, ops_cap * 2));
	cr_assert(eq(sz, pool.free_cap, free_cap * 4));
	cr_assert(eq(sz, pool.free_len, 0));

	op_pool_deinit(&pool);
}
