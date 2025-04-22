#ifndef OP_H
#define OP_H

#include <stdbool.h>
#include <arpa/inet.h>

typedef enum op_type {
	OP_ACCEPT,
	OP_READ,
	OP_WRITE,
} OpType;

typedef struct op {
	size_t pool_id; /* MUST NOT BE MODIFIED. */

	uint64_t client_id;

	size_t buf_cap; /* Total capcity of buf. */
	size_t buf_len; /* Number of bytes used for the operation. */
	char *buf; /* Operation doesn't own the buf. */

	size_t processed; /* Used for handling short writes. */

	int client_fd; /* Operation doesn't manage the socket. */

	OpType type;

} Operation;

char *op_type_str(OpType type);

bool op_is_incomplete(Operation *op, size_t processed);

#endif
