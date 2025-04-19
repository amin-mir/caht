#ifndef OP_POOL_H
#define OP_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "op.h"

typedef struct op_pool {
	/* buf_len that is used when creating operations. */
	size_t ops_buf_len; 

	/**
	 * Next index in ops which is going to be allocated. It can only be incremented
	 * and once it reaches ops_cap we will need to grow the ops.
	 */
	size_t ops_next_idx;

	size_t ops_cap;
	Operation **ops; /* Array of Operation ptrs. */

	/** 
	 * Len is incremented and decremented as operations are fetched
	 * and returned to the pool.
	 */
	size_t free_len; 
	size_t free_cap;

	/* Indicates which slots are free in ops array. */
	size_t *free_ops_idx; 
} OpPool;

void op_pool_init_with_cap(
	OpPool *pool, 
	size_t ops_buf_len, 
	size_t op_cap, 
	size_t free_cap
);
void op_pool_init(OpPool *pool, size_t ops_buf_len);
void op_pool_deinit(OpPool *pool);

/* Assumes that pool_id was acquired through op_pool_new_entry. */
Operation *op_pool_get(OpPool *pool, uint64_t pool_id);

/**
 * Tries to grab an already allocated Operation that has been returned to the pool
 * most recently first. If free list is empty, allocates a new Operation and appends
 * it to list of operations.
 */
Operation *op_pool_get_new(OpPool *pool, uint64_t client_id);

void op_pool_put(OpPool *pool, Operation *op);

#endif
