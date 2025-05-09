#ifndef OP_H
#define OP_H

#include <stdbool.h>
#include <arpa/inet.h>

#include "slab.h"

typedef enum op_type {
	OP_ACCEPT,
	OP_READ,
	OP_WRITE,
} OpType;

typedef struct op {
	/* MUST NOT BE MODIFIED. */
	size_t pool_id; 

	OpType type;
	uint64_t client_id;

	/** 
	 * Operation doesn't own the socket. A single socket could be involved
	 * with several operations.
	 */
	int client_fd; 

	size_t buf_cap; /* Total capcity of buf. */
	size_t buf_len; /* Number of bytes used for the operation. */
	BufRef *buf_ref; /* Operation doesn't own the buf. */
	size_t processed; /* Used for handling short writes. */
} Operation;

char *op_type_str(OpType type);

bool op_is_incomplete(Operation *op, size_t processed);

#endif
