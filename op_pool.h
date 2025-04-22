/**
 * OpPool contract:
 *
 * it assigns a pool_id to each Operation once they are allocated and it requires
 * pool_id to not change by the users.
 *
 * it requires users to manage buf and client_fd. Before returning an entry
 * to the pool, buf must be set to NULL and client_fd to -1;
 *
 * When pool returns a new entry it sets both buf to NULL and client_fd to -1;
 */

#ifndef OP_POOL_H
#define OP_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "op.h"

typedef struct op_pool {
	/**
	 * Next index in ops which is going to be allocated. It can only be incremented
	 * and once it reaches ops_cap we will need to grow the ops.
	 */
	size_t ops_next_idx;

	size_t ops_cap;
	Operation **ops; /* Array of Operation ptrs. */

	/**
	 * Len is incremented and decremented as operations are fetched and returned
	 * to the pool.
	 */
	size_t free_len; 
	size_t free_cap;

	/* Indicates which slots are free in ops array. */
	size_t *free_ops_idx; 
} OpPool;

void op_pool_init_with_cap(
	OpPool *pool, 
	size_t op_cap, 
	size_t free_cap
);
void op_pool_init(OpPool *pool);
void op_pool_deinit(OpPool *pool);

/**
 * Assumes that pool_id was acquired through op_pool_new_entry. Doesn't check
 * whether the matching operation is in use or not.
 */
Operation *op_pool_get(OpPool *pool, uint64_t pool_id);

/**
 * Inserts a new operation in the pool and returns a ptr to it.
 *
 * Tries to grab an already allocated Operation that has been returned to the pool
 * most recently first. If free list is empty, allocates a new Operation and appends
 * it to list of operations.
 */
Operation *op_pool_new_entry(OpPool *pool);

void op_pool_return(OpPool *pool, Operation *op);

#endif
