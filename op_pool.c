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
	size_t ops_cap, 
	size_t free_cap
) {
	pool->ops_cap = ops_cap;
	pool->ops_next_idx = 0;
	pool->ops = must_malloc(
		ops_cap * sizeof(Operation *),
		"op_pool_init_with_cap malloc ops"
	);

	pool->free_cap = free_cap;
	pool->free_len = 0;
	pool->free_ops_idx = must_malloc(
		free_cap * sizeof(size_t), 
		"op_pool_init_with_cap malloc free_ops_idx"
	);
}

void op_pool_init(OpPool *pool) {
	op_pool_init_with_cap(pool, OPERATIONS_INIT_CAP, FREE_OPS_INIT_CAP);
}

void op_pool_deinit(OpPool *pool) {
	for (size_t i = 0; i < pool->ops_next_idx; i++) {
		free(pool->ops[i]);
	}
	free(pool->ops);
	free(pool->free_ops_idx);
}

Operation *op_pool_get(OpPool *pool, size_t pool_id) {
	assert(pool_id < pool->ops_next_idx); /* bounds checking */
	return pool->ops[pool_id];
}

Operation *op_pool_new_entry(OpPool *pool) {
	if (pool->free_len != 0) {
		/* Grab the most recent freed entry. */
		size_t pool_id = pool->free_ops_idx[pool->free_len - 1];
		pool->free_len--;

		/* pool_id must not have changed. */
		Operation *op = pool->ops[pool_id];

		/**
		 * OpPool contract:
		 * pool_id value should not have changed. client_fd and buf are checked
		 * when Operation is returned to the pool.
		 */
		assert(op->pool_id == pool_id);
		return op;
	}

	if (pool->ops_next_idx == pool->ops_cap) {
		pool->ops_cap *= 2;
		pool->ops = must_realloc(
			pool->ops, 
			pool->ops_cap * sizeof(Operation *),
			"op_pool_new_entry realloc ops"
		);
	}

	Operation *op = must_malloc(
		sizeof(Operation), 
		"op_pool_new_entry malloc op"
	);

	/**
	 * OpPool contract:
	 * assign correct value to pool_id and ensure client_fd and buf have correct
	 * initial values.
	 */
	op->pool_id = pool->ops_next_idx;
	op->client_fd = -1;
	op->buf = NULL;

	pool->ops[pool->ops_next_idx] = op;
	pool->ops_next_idx++;

	return op;
}

void op_pool_return(OpPool *pool, Operation *op) {
	/**
	 * Because Operation doesn't manage the lifetime of op->buf, we don't
	 * need to free that here. The buffers come from the slab, and the slab
	 * is in charge of freeing them.
	 *
	 * Operation isn't in charge of closing the socket either. We could have
	 * several operations which are for instance sending to the same client_fd.
	 * client_fd must only be closed once and when it's removed from ClientMap.
	 *
	 * The only thing that is relevant to the pool is the pool_id which must not
	 * be modified externally. Freeing the buffer and closing the socket should be
	 * performed by users of pool, that's why we assert to ensure they have taken
	 * care of these things here.
	 */
	assert(op->buf == NULL);
	assert(op->client_fd == -1);

	if (pool->free_len == pool->free_cap) {
		pool->free_cap = pool->free_cap * 2;
		pool->free_ops_idx = must_realloc(
			pool->free_ops_idx, 
			pool->free_cap * sizeof(size_t),
			"pool_put realloc free_ops_idx"
		);
	}
	pool->free_ops_idx[pool->free_len] = op->pool_id;
	pool->free_len++;
}
