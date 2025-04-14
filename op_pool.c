#include <assert.h>

#include <stddef.h>
#include <stdio.h>

#include "op.h"
#include "op_pool.h"
#include "utils.h"

#define MAX_CLIENTS 1024

/* Max uint64_t we can reach with CLIENT_ID_BITS bits. */
#define MAX_CLIENT_ID ((1UL << CLIENT_ID_BITS) - 1)

static uint64_t next_client_id = 0;

/* Once we malloc a `struct op` and place a ptr to it in this array, we won't
 * free that memory, and instead will keep using it for new connections when the
 * client disconnects. */
static struct op *pool_ops[MAX_CLIENTS] = {NULL};
static uint16_t next_pool_idx = 0;

/* Keeps track of the available slots in client_ops. */
static uint16_t free_pool_entries[MAX_CLIENTS];
static uint16_t free_pool_entries_len = 0;

/* pool_id embeds several things:
 * client_id: 53 most significant bits
 * pool_idx:	the next 10 bits
 * in_use:		the least significant bit
 *
 * The returned id is marked as in use.
 */
static inline uint64_t make_pool_id(uint64_t client_id, uint16_t pool_idx) {
	return (client_id << SHFT_CLIENT_ID) |
				 ((pool_idx & POOL_IDX_MASK) << SHFT_POOL_IDX) | 1;
}

struct op *pool_get(uint64_t pool_id) {
	uint16_t pool_idx = extract_pool_idx(pool_id);
	return pool_ops[pool_idx];
}

struct op *pool_pick_free() {
	if (free_pool_entries_len != 0) {
		/* Swap the first element with the last, and decrement len by 1. */
		uint16_t pool_idx = free_pool_entries[0];
		free_pool_entries[0] = free_pool_entries[free_pool_entries_len - 1];
		free_pool_entries_len--;

		uint64_t id = make_pool_id(next_client_id, pool_idx);
		// printf("new pool id: %lu made with client_id=%lu pool_idx=%u\n", id,
		//				next_client_id, next_pool_idx);

		/* more efficient way to do: (next_client_id + 1) % MAX_CLIENT_ID */
		next_client_id = (next_client_id + 1) & MAX_CLIENT_ID;

		/* Assign the id to this op marking it as used by a new client_id. */
		struct op *fop = pool_ops[pool_idx];
		fop->pool_id = id;

		// printf("reusing op pool_idx=%d id=%lu old_fd = %d\n", pool_idx, id,
		//				pool_ops[pool_idx]->client_fd);

		return pool_ops[pool_idx];
	}

	/* We will initialize all of client_ops with a single run. Once all elements
	 * have been allocated, there is no point going from the beginning by doing:
	 *
	 * `next_op_idx = (next_op_idx + 1) % MAX_CLIENTS;`
	 *
	 * If we couldn't acquire a free slot in the last step, it means we're serving
	 * max allowed clients already.
	 */
	if (next_pool_idx == MAX_CLIENTS)
		return NULL;

	assert(pool_ops[next_pool_idx] == NULL);

	uint64_t id = make_pool_id(next_client_id, next_pool_idx);
	// printf("new pool id: %lu made with client_id=%lu pool_idx=%u\n", id,
	//				next_client_id, next_pool_idx);

	/* more efficient way to do: (next_client_id + 1) % MAX_CLIENT_ID */
	next_client_id = (next_client_id + 1) & MAX_CLIENT_ID;

	struct op *fop = op_create_accept(id);
	pool_ops[next_pool_idx] = fop;
	next_pool_idx++;

	return fop;
}

void pool_put(struct op *fop, uint64_t pool_id) {
	uint64_t client_id = extract_client_id(fop->pool_id);
	uint16_t pool_idx = extract_pool_idx(fop->pool_id);
	if (fop->pool_id == pool_id) {
		printf("closing socket=%d id=%lu client_id=%lu pool_idx=%u\n",
					 fop->client_fd, pool_id, client_id, pool_idx);
		fop->pool_id = clear_in_use(fop->pool_id);
		must_close(fop->client_fd);
		free_pool_entries[free_pool_entries_len] = pool_idx;
		free_pool_entries_len++;
	} else {
		printf("skip close id=%lu\n", fop->pool_id);
	}
}
