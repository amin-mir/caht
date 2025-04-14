#ifndef OP_POOL_H
#define OP_POOL_H

#include <stdint.h>
#include <stdbool.h>

#define POOL_IDX_BITS 10

/* Remaining bits in a uint64_t - (index bits + used bit) */
#define CLIENT_ID_BITS (sizeof(uint64_t) * 8 - POOL_IDX_BITS - 1)

#define SHFT_POOL_IDX 1
#define SHFT_CLIENT_ID (POOL_IDX_BITS + 1)

/* Used for marking the op as unused by performing `&`. */
#define CLEAR_LSB_MASK (~1UL)

/* Used when we need to keep only the pool index related bits. */
#define POOL_IDX_MASK ((1UL << POOL_IDX_BITS) - 1)

/* Extract in_use bit. */
static inline bool extract_in_use(uint64_t op_id) { return op_id & 1UL; }

static inline uint64_t clear_in_use(uint64_t op_id) {
	return op_id & CLEAR_LSB_MASK;
}

/* Extract 10 bits for pool_idx. */
static inline uint16_t extract_pool_idx(uint64_t op_id) {
	return (op_id >> SHFT_POOL_IDX) & POOL_IDX_MASK;
}

/* Extract 53 MSB bits for client_id. */
static inline uint64_t extract_client_id(uint64_t op_id) { return op_id >> SHFT_CLIENT_ID; }

struct op *pool_get(uint64_t pool_id);

/* Returns a free struct op * and marks it as in-use.
 * Returns NULL in case of error.
 */
struct op *pool_pick_free();

/* Checks two conditions before returning op to the pool:
 *	 1. op blongs to the client id which is extracted from pool_id.
 *	 2. op is marked as unused.
 */
void pool_put(struct op *op, uint64_t pool_id);

#endif
