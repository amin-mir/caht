#include <assert.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "op_pool.h"
#include "utils.h"

#define OPERATIONS_INIT_CAP 1024
#define FREE_OPS_INIT_CAP 256

void op_pool_init_with_cap(
	OpPool *pool, 
	size_t ops_buf_len, 
	size_t ops_cap, 
	size_t free_cap
) {
	pool->ops_buf_len = ops_buf_len;
	pool->ops_cap = ops_cap;
	pool->ops_next_idx = 0;
	pool->ops = must_calloc(
		ops_cap, 
		sizeof(Operation *),
		"op_pool_init_with_cap calloc ops"
	);

	pool->free_cap = free_cap;
	pool->free_len = 0;
	pool->free_ops_idx = must_calloc(
		free_cap, 
		sizeof(size_t), 
		"op_pool_init_with_cap calloc free_ops_idx"
	);
}

void op_pool_init(OpPool *pool, size_t ops_buf_len) {
	op_pool_init_with_cap(pool, ops_buf_len, OPERATIONS_INIT_CAP, FREE_OPS_INIT_CAP);
}

void op_pool_deinit(OpPool *pool) {
	for (size_t i = 0; i < pool->ops_next_idx; i++) {
		op_destroy(pool->ops[i]);
	}
	free(pool->ops);
	free(pool->free_ops_idx);
}

Operation *op_pool_get(OpPool *pool, size_t pool_id) {
	assert(pool_id < pool->ops_next_idx); /* bounds checking */
	return pool->ops[pool_id];
}

Operation *op_pool_get_new(OpPool *pool, uint64_t client_id) {
	if (pool->free_len != 0) {
		/* Grab the most recent freed entry. */
		size_t pool_id = pool->free_ops_idx[pool->free_len - 1];
		pool->free_len--;

		/* We need assign a new client_id but pool_id stays the same. */
		Operation *fop = pool->ops[pool_id];
		assert(fop->pool_id == pool_id);
		fop->client_id = client_id;

		return fop;
	}

	if (pool->ops_next_idx == pool->ops_cap) {
		size_t old_cap = pool->ops_cap;
		size_t new_cap = pool->ops_cap * 2;
		pool->ops = must_realloc(
			pool->ops, 
			new_cap * sizeof(Operation *),
			"op_pool_get_new realloc ops"
		);

		/* Ensuring that during realloc, new mem locations are 0 initialized. */
		memset(pool->ops + old_cap, 0, (new_cap - old_cap) * sizeof(Operation *));
		pool->ops_cap = new_cap;
	}

	assert(pool->ops[pool->ops_next_idx] == NULL);

	Operation *fop = op_create_accept(
		client_id, 
		pool->ops_next_idx, 
		pool->ops_buf_len
	);
	pool->ops[pool->ops_next_idx] = fop;
	pool->ops_next_idx++;

	return fop;
}

void op_pool_put(OpPool *pool, Operation *op) {
	if (op->client_fd > 0) {
		printf("closing socket=%d\n", op->client_fd);
		must_close(op->client_fd, "pool_put close client_fd");
	}
	op->client_fd = -1;
	if (pool->free_len == pool->free_cap) {
		pool->free_cap = pool->free_cap * 2;
		pool->free_ops_idx = must_realloc(
			pool->free_ops_idx, 
			pool->free_cap * sizeof(size_t),
			"pool_put realloc free_ops_idx"
		);

		/**
		 * unlike ops, We don't need to 0 initialize the new elements for free_ops_idx
		 * because it doesn't hold ptrs that need to be set to NULL initially, and we
		 * only access the elements before the free_len which must hold correct values.
		 */
	}
	pool->free_ops_idx[pool->free_len] = op->pool_id;
	pool->free_len++;
}
