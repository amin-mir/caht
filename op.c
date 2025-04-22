#include "op.h"

char *op_type_str(OpType type) {
	switch (type) {
	case OP_ACCEPT:
		return "ACCEPT";
	case OP_READ:
		return "READ";
	case OP_WRITE:
		return "WRITE";
	default:
		return "UNKNOWN";
	}
}

bool op_is_incomplete(Operation *op, size_t processed) {
	size_t requested = op->buf_len - op->processed;
	return processed < requested;
}
