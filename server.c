#include <liburing.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>

#include "protocol.h"
#include "server.h"

#define CQE_BATCH_SIZE 32

static uint64_t next_client_id = 1;

bool username_valid(size_t len, const char username[len]) {
	for (size_t i = 0; i < len; i++) {
		char c = username[i];
		if (!isalnum(c)) return false;
	}
	return true;
}

void log_with_client_info(int client_fd, ClientInfo *info, char *msg) {
	char client_ip[INET_ADDRSTRLEN];
	const char *dst = inet_ntop(
		AF_INET, 
		&info->client_addr.sin_addr, 
		client_ip, 
		INET_ADDRSTRLEN
	);
	if (dst == NULL) fatal_error("inet_ntop");
	uint16_t port = ntohs(info->client_addr.sin_port);

	printf("[%s:%d] client_id=%lu client_fd=%d => %s\n", 
		client_ip, port,
		info->client_id, client_fd, msg
	);
}


void acquire_send_buf(Server *srv, size_t len, Operation *op) {
	Slab *s = srv->slab64;
	if (len > 64) {
		s = srv->slab2k;
	}
	op->buf = slab_get(s);
	assert(op->buf != NULL);
	op->buf_cap = slab_buf_cap(s);
	op->buf_len = len;
}

/* Make sure to assign the len to buf_len as this function doesn't do that. */
void acquire_small_send_buf(Server *srv, Operation *op) {
	op->buf = slab_get(srv->slab64);
	assert(op->buf != NULL);
	op->buf_cap = slab_buf_cap(srv->slab64);
}

void free_op(Server *srv, Operation *op) {
	Slab *s = srv->slab64;
	if (op->buf_cap > 64) {
		s = srv->slab2k;
	}
	slab_put(s, op->buf);

	/* POOL CONTRACT */
	op->client_fd = -1;
	op->buf = NULL;
	op_pool_return(srv->pool, op);
}

void disconnect_and_free_op(Server *srv, ClientInfo *info, Operation *op) {
	/**
	 * DISCONNECT
	 *
	 * Avoid closing the socket multiple times. If client info is NULL it means an
	 * operation has already failed on this socket or client closed the socket on their part,
	 * and client information has already been removed and the corresponding client_fd closed.
	 */
	if (info != NULL) {
		log_with_client_info(op->client_fd, info, "disconnected");
		must_close(op->client_fd, "handle_recv close client_fd");
		client_map_delete(srv->clients, op->client_id);
	}

	/* FREE OPERATION */
	free_op(srv, op);

	/* TODO: RATE LIMITING Accept more connections.
	 * rh_add_accept(rh, next_client_id);
	 * next_client_id++;
	 */
}

/* Make sure to assign the len to buf_len as this function doesn't do that. */
void acquire_large_send_buf(Server *srv, Operation *op) {
	op->buf = slab_get(srv->slab2k);
	assert(op->buf != NULL);
	op->buf_cap = slab_buf_cap(srv->slab2k);
}

Server server_init(struct io_uring *ring, ClientMap *clients, Slab *slab64, Slab *slab2k,
			   OpPool *pool, int server_fd)
{
	return (Server){
		.ring = ring,
		.clients = clients,
		.slab64 = slab64,
		.slab2k = slab2k,
		.pool = pool,
		.server_fd = server_fd,
	};
}

void add_accept(Server *srv, uint64_t client_id) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(srv->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	Operation *op = op_pool_new_entry(srv->pool);
	assert(op != NULL);

	/**
	 * op->pool_id should not be modified.
	 *
	 * Directly getting a buffer from slab2k instead of calling acquire_op_buf because
	 * the buffer will be used for recv operations and we want it to be as large as possible.
	 */
	op->buf = slab_get(srv->slab2k);
	assert(op->buf != NULL);
	op->buf_len = 0;
	op->buf_cap = slab_buf_cap(srv->slab2k);
	op->client_id = client_id;
	op->processed = 0;
	op->client_fd = -1;
	op->type = OP_ACCEPT;

	ClientInfo *info;
	client_map_new_entry(srv->clients, client_id, &info);
	memset(&info->client_addr, 0, sizeof(info->client_addr));
	info->client_addr_len = sizeof(info->client_addr);

	printf("ADD ACCEPT client_id=%lu\n", client_id);

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	/* Setting SOCK_NONBLOCK saves us extra calls to fcntl. */
	io_uring_prep_accept(
		sqe, 
		srv->server_fd, 
		(struct sockaddr *)&info->client_addr,
		&info->client_addr_len, SOCK_NONBLOCK
	);
}

/*
int add_read_with_timeout(
	IoUring *ring, 
	Operation *read_op, 
	int timeout_ms
) {
	 struct io_uring_sqe *sqe_read = io_uring_get_sqe(ring);
	 if (!sqe_read) {
			 fprintf(stderr, "SQ is full\n");
			 return -1;
	 }

	 read_op->type = OP_READ;
	 read_op->buf = must_malloc(BUF_SIZE);
	 read_op->buflen = BUF_SIZE;
	 io_uring_sqe_set_data(sqe_read, read_op);
	 io_uring_prep_recv(sqe_read, read_op->client_fd, read_op->buf,
	 read_op->buflen, 0);

	 // Fetch another SQE for the timeout 
	 struct io_uring_sqe *sqe_timeout = io_uring_get_sqe(ring);
	 if (!sqe_timeout) {
			 fprintf(stderr, "SQ is full\n");
			 return -1;
	 }

	 struct __kernel_timespec ts;
	 ts.tv_sec = timeout_ms / 1000;
	 ts.tv_nsec = (timeout_ms % 1000) * 1000000; // Convert milliseconds to
	 nanoseconds

	 IORING_OP_LINK_TIMEOUT
	 io_uring_prep_link_timeout(sqe_timeout, &ts, 0, 0);

	 // Mark the read request as linked to the timeout
	 sqe_read->flags |= IOSQE_IO_LINK;

	 // Submit both SQEs as a linked operation
	 if (io_uring_submit(ring) < 0)
			 fatal_error("io_uring_submit");

	 return 0;
}
*/

/**
 * Essentially client_fd is only needed when this function is called for the first time,
 * because we only get the client_fd after a successful accept. But in order to keep the
 * code style consistent, we avoid assigning the client_fd outside of this functions.
 */
void add_recv(Server *srv, Operation *op, int client_fd) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(srv->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	/**
	 * pool_id:    must not be modified.
	 * client_id:  set in add_accept.
	 * buf_cap:    set in add_accept.
	 * buf_len:    set in add_accept.
	 * buf:        set in add_accept.
	 * processed:  not needed.
	 */
	op->client_fd = client_fd;
	op->type = OP_READ;

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	io_uring_prep_recv(sqe, op->client_fd, op->buf, op->buf_cap, 0);
}

void resume_recv(Server *srv, Operation *op, size_t bytes_read) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(srv->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	/**
	 * pool_id:    must not be modified.
	 * client_id:  set in add_accept.
	 * buf_cap:    set in add_accept.
	 * buf_len:    set in add_accept.
	 * buf:        set in add_accept.
	 * type:       set in add_read.
	 * client_fd:  set in add_read.
	 * processed:  not needed.
	 */

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	char *buf = op->buf + bytes_read;
	size_t len = op->buf_cap - bytes_read;
	io_uring_prep_recv(sqe, op->client_fd, buf, len, 0);
}

void add_send(Server *srv, Operation *op, int client_fd, uint64_t client_id) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(srv->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	/* op->pool_id should not be modified. */
	op->client_id = client_id;
	op->processed = 0;
	op->client_fd = client_fd;
	op->type = OP_WRITE;

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	io_uring_prep_send(sqe, op->client_fd, op->buf, op->buf_len, 0);
}

void resume_send(Server *srv, Operation *op, size_t processed) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(srv->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	/**
	 * pool_id:   should not be modified.
	 * client_id: set in add_send.
	 * buf_cap:   set in add_send.
	 * buf_len:   set in add_send.
	 * buf:       set in add_send.
	 * client_fd: set in add_send.
	 * type:      set in add_send.
	 */
	op->processed += processed;

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	char *buf = op->buf + op->processed;
	size_t len = op->buf_len - op->processed;
	io_uring_prep_send(sqe, op->client_fd, buf, len, 0);
}

void send_server_error(Server *srv, int client_fd, uint64_t client_id,
					   uint64_t seqid, uint8_t code)
{
	Operation *op = op_pool_new_entry(srv->pool); 
	assert(op != NULL);
	acquire_small_send_buf(srv, op);

	op->buf_len = ser_server_error(op->buf_cap, op->buf, seqid, code);
	add_send(srv, op, client_fd, client_id);
}

void send_set_username_response(Server *srv, int client_fd, uint64_t seqid,
								uint64_t client_id)
{
	Operation *op = op_pool_new_entry(srv->pool); 
	assert(op != NULL);
	acquire_small_send_buf(srv, op);

	op->buf_len = ser_set_username_response(op->buf_cap, op->buf, seqid);
	add_send(srv, op, client_fd, client_id);
}

/**
 * rh_handle will add sqes but will not submit. We assume ring will have
 * enough room for the resulting sqes. However, for massive groups the job has
 * to be handles in several stages in order to avoid overflowing the sqe buffer.
 *
 * req_len is essentially the prefix of the each message already parsed by the
 * caller of this function. We assume req_len >= 3 and req should contain all bytes 
 * necessary for parsing a request.
 */
int handle(Server *srv, ClientInfo *info, Operation *req_op, char *req_buf,
		   size_t req_len)
{
	int client_fd = req_op->client_fd;
	uint64_t client_id = req_op->client_id;

	char msg_type = req_buf[PROT_MSGT_OFFT];
	size_t remaining = req_len - PROT_HDR_LEN;
	switch (msg_type) {
		case MSGT_SET_USERNAME: {
			/* Network byte order to host byte order. */
			uint64_t seqid;
			const char *uname;
			size_t uname_len;
			deser_set_username_request(req_buf, &seqid, &uname, &uname_len);

			/* Ensure username len is in range [3, 15]. */
			if (uname_len < 3 || uname_len > 15) {
				uint8_t code = CODE_INVALID_MSG_LEN;
				send_server_error(srv, client_fd, client_id, seqid, code);
				return 0;
			}

			/* Ensure username chars are valid. */
			if (!username_valid(uname_len, uname)) {
				uint8_t code = CODE_INVALID_USERNAME;
				send_server_error(srv, client_fd, client_id, seqid, code);
				return 0;
			}
			
			memcpy(info->username, uname, uname_len);
			info->username[remaining] = '\0';

			send_set_username_response(srv, client_fd, seqid, client_id);
			return 0;
		}
		default: {
			return -1;
		}
	};
}

void handle_accept(Server *srv, int client_fd, Operation *op) {
	/* Log connection info. */
	ClientInfo *info = client_map_get(srv->clients, op->client_id);
	assert(info != NULL);
	log_with_client_info(client_fd, info, "connected");

	/* Recv from connected socket. */
	add_recv(srv, op, client_fd);

	/* Accept more connections. */
	add_accept(srv, next_client_id);
	next_client_id++;
}

void handle_recv(Server *srv, ClientInfo *info, Operation *op, size_t bytes_read) {
	if (bytes_read == 0) {
		disconnect_and_free_op(srv, info, op);
		return;
	}

	char *req_buf = op->buf;
	uint16_t net_len;
	while (bytes_read > PROT_HDR_LEN) {
		memcpy(&net_len, req_buf, sizeof(net_len));
		uint16_t req_len = ntohs(net_len);

		/* Received request is incomplete. */
		if (bytes_read < req_len) {
			/* No need for memmove if we haven't handled anything. */
			if (op->buf != req_buf) {
				memmove(op->buf, req_buf, op->buf_cap - bytes_read);
			}
			break;
		}

		/* There's enough bytes to parse a request. */
		int ret = handle(srv, info, op, req_buf, req_len);
		if (ret == -1) {
			disconnect_and_free_op(srv, info, op);
			return;
		}

		bytes_read -= req_len;
		req_buf += req_len;
	}

	resume_recv(srv, op, bytes_read);
}

void handle_send(Server *srv, ClientInfo *info, Operation *op, size_t bytes_written) {
	if (bytes_written == 0) {
		log_with_client_info(op->client_fd, info, "SHORT_WRITE_0");
	}

	if (op_is_incomplete(op, bytes_written)) {
		resume_send(srv, op, bytes_written);
		return;
	}
	
	free_op(srv, op);
}

void handle_cqe_batch(Server *srv, struct io_uring_cqe *cqes[], int count) {
	for (int i = 0; i < count; i++) {
		struct io_uring_cqe *cqe = cqes[i];

		uint64_t pool_id = cqe->user_data;
		int cqe_res = cqe->res;
		/* DO NOT TOUCH CQE AFTER THIS POINT. */
		io_uring_cqe_seen(srv->ring, cqe);

		Operation *op = op_pool_get(srv->pool, pool_id);
		assert(op != NULL);

		ClientInfo *info = client_map_get(srv->clients, op->client_id);

		/* If an operation fails, server disconnects the client and frees the op. */
		if (cqe_res < 0) {
			fprintf(stderr,
				"[fd=%d client_id=%lu] op %s failed: %s\n",
				op->client_fd, op->client_id,
				op_type_str(op->type), strerror(-cqe_res)
			);
			disconnect_and_free_op(srv, info, op);
			continue;
		}

		/**
		 * It is possible for the operation to succeed but the server has already dropped
		 * this client e.g. this operation is a successful send, but server received
		 * a malformed message from this client_id and already dropped its connection.
		 */
		if (info == NULL) {
			printf(
				"[fd=%d client_id=%lu] successful op %s but client already disconnected.\n",
				op->client_fd, op->client_id,
				op_type_str(op->type)
			);
			free_op(srv, op);
			continue;
		}

		switch (op->type) {
			case OP_ACCEPT: {
				int client_fd = cqe_res;
				handle_accept(srv, client_fd, op);
				break;
			}
			case OP_READ: {
				/* cqe_res isn't negative so it's safe to assign to size_t. */
				size_t bytes_read = cqe_res;
				handle_recv(srv, info, op, bytes_read);
				break;
			}
			case OP_WRITE: {
				/* cqe_res isn't negative so it's safe to assign to size_t. */
				size_t bytes_written = cqe_res;
				handle_send(srv, info, op, bytes_written);
				break;
			}
			default: {
				printf("invalid operation type: %s\n", op_type_str(op->type));
			}
		}
	}
}

int server_start(Server *srv) {
	int ret;
	struct io_uring_cqe *cqes[CQE_BATCH_SIZE];
	
	add_accept(srv, next_client_id);
	next_client_id++;

	if ((ret = io_uring_submit(srv->ring)) < 0) {
		fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
		exit(EXIT_FAILURE);
	} 

	while (1) {
		if ((ret = io_uring_wait_cqe(srv->ring, &cqes[0])) < 0) {
			fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
			exit(EXIT_FAILURE);
		}
		
		handle_cqe_batch(srv, cqes, 1);

		int count = io_uring_peek_batch_cqe(srv->ring, cqes, CQE_BATCH_SIZE);
		if (count == -EAGAIN) {
			/* Submit the result of handling cqe from io_uring_wait_cqe. */
			if ((ret = io_uring_submit(srv->ring)) < 0) {
				fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
				exit(EXIT_FAILURE);
			}
			continue;
		}
		if (count < 0) {
			fprintf(stderr, "io_uring_peek_batch_cqe: %s\n", strerror(-ret));
			exit(EXIT_FAILURE);
		}

		handle_cqe_batch(srv, cqes, count);

		if ((ret = io_uring_submit(srv->ring)) < 0) {
			fprintf(stderr, "io_uring_submit %s\n", strerror(-ret));
			exit(EXIT_FAILURE);
		}
	}
}
