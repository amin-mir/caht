#ifndef OP_H
#define OP_H

#include <stdbool.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

enum op_type {
	OP_ACCEPT,
	OP_READ,
	OP_WRITE,
};

struct op {
	uint64_t pool_id;

	/* Buffer that's used to get bytes in and out. */
	char *buf;
	size_t buf_len;

	/* Used for handling short writes. */
	size_t processed;

	/* Used for storing client address. */
	struct sockaddr_in client_addr;
	socklen_t client_addr_len;

	/* Client file descryptor. */
	int client_fd;

	/* Type of current operation to perform for this client. */
	enum op_type type;
	
	char username[16];
};

char *op_type_str(enum op_type type);

/* op returned from this function is marked as in_use. */
struct op *op_create_accept(uint64_t id);

void op_destroy(struct op *op);

void op_log_with_client_ip(struct op *op, char *msg);

bool op_is_incomplete(struct op *op, size_t processed);

#endif
