#include <arpa/inet.h>
#include <assert.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "client_map.h"
#include "op.h"
#include "op_pool.h"
#include "slab.h"
#include "utils.h"
#include "protocol.h"

#define QUEUE_SIZE 4096
#define CQE_BATCH_SIZE 32
#define BACKLOG 10
#define PORT 8080

static uint64_t next_client_id = 1;

int setup_server(int port) {
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) fatal_error("setup_server socket()");

	int enable = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
		fatal_error("setsockopt(SO_REUSEADDR)");

	set_nonblocking(server_fd);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int ret = bind(
		server_fd, 
		(struct sockaddr *)&server_addr,
		sizeof(server_addr)
	);

	if (ret == -1) fatal_error("setup_server bind");

	if (listen(server_fd, BACKLOG) == -1) fatal_error("setup_server listen");

	return server_fd;
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

	printf(
		"[%s:%d] client_id=%lu client_fd=%d => %s\n", 
		client_ip, 
		port, 
		info->client_id, 
		client_fd, 
		msg
	);
}

void handle_accept(
	RequestHandler *rh,
	int client_fd,
	Operation *op
) {
	/* Log connection info. */
	ClientInfo *info = client_map_get(rh->clients, op->client_id);
	assert(info != NULL);
	log_with_client_info(client_fd, info, "connected");

	/* Recv from connected socket. */
	rh_add_recv(rh, op, client_fd);

	/* Accept more connections. */
	rh_add_accept(rh, next_client_id);
	next_client_id++;
}

void free_op(RequestHandler *rh, Operation *op) {
	Slab *s = rh->slab64;
	if (op->buf_cap > 64) {
		s = rh->slab2k;
	}
	slab_put(s, op->buf);

	/* POOL CONTRACT */
	op->client_fd = -1;
	op->buf = NULL;
	op_pool_return(rh->pool, op);
}

void disconnect_and_free_op(
	RequestHandler *rh,
	ClientInfo *info,
	Operation *op
) {
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
		client_map_delete(rh->clients, op->client_id);
	}

	/* FREE OPERATION */
	free_op(rh, op);

	/* TODO: RATE LIMITING Accept more connections.
	 * rh_add_accept(rh, next_client_id);
	 * next_client_id++;
	 */
}

void handle_recv(
	RequestHandler *rh,
	ClientInfo *info,
	Operation *op,
	size_t bytes_read
) {
	if (bytes_read == 0) {
		disconnect_and_free_op(rh, info, op);
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
		int ret = rh_handle(rh, info, op, req_buf, req_len);
		if (ret == -1) {
			disconnect_and_free_op(rh, info, op);
			return;
		}
		bytes_read -= req_len;
		req_buf += req_len;
	}

	rh_resume_recv(rh, op, bytes_read);
}

void handle_send(
	RequestHandler *rh, 
	ClientInfo *info, 
	Operation *op, 
	size_t bytes_written
) {
	if (bytes_written == 0) {
		log_with_client_info(op->client_fd, info, "SHORT_WRITE_0");
	}

	if (op_is_incomplete(op, bytes_written)) {
		rh_resume_send(rh, op, bytes_written);
		return;
	}
	
	free_op(rh, op);
}

void handle_cqe_batch(RequestHandler *rh, struct io_uring_cqe *cqes[], int count) {
	for (int i = 0; i < count; i++) {
		struct io_uring_cqe *cqe = cqes[i];

		uint64_t pool_id = cqe->user_data;
		int cqe_res = cqe->res;
		/* DO NOT TOUCH CQE AFTER THIS POINT. */
		io_uring_cqe_seen(rh->ring, cqe);

		Operation *op = op_pool_get(rh->pool, pool_id);
		assert(op != NULL);

		ClientInfo *info = client_map_get(rh->clients, op->client_id);

		/* If an operation fails, server disconnects the client and frees the op. */
		if (cqe_res < 0) {
			fprintf(stderr,
				"[fd=%d client_id=%lu] op %s failed: %s\n",
				op->client_fd, op->client_id,
				op_type_str(op->type), strerror(-cqe_res)
			);
			disconnect_and_free_op(rh, info, op);
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
			free_op(rh, op);
			continue;
		}

		switch (op->type) {
			case OP_ACCEPT: {
				int client_fd = cqe_res;
				handle_accept(rh, client_fd, op);
				break;
			}
			case OP_READ: {
				/* cqe_res isn't negative so it's safe to assign to size_t. */
				size_t bytes_read = cqe_res;
				handle_recv(rh, info, op, bytes_read);
				break;
			}
			case OP_WRITE: {
				/* cqe_res isn't negative so it's safe to assign to size_t. */
				size_t bytes_written = cqe_res;
				handle_send(rh, info, op, bytes_written);
				break;
			}
			default: {
				printf("invalid operation type: %s\n", op_type_str(op->type));
			}
		}
	}
}

int start_server(RequestHandler *rh) {
	int ret;
	struct io_uring_cqe *cqes[CQE_BATCH_SIZE];
	
	rh_add_accept(rh, next_client_id);
	next_client_id++;

	if ((ret = io_uring_submit(rh->ring)) < 0) {
		fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
		exit(EXIT_FAILURE);
	} 

	while (1) {
		if ((ret = io_uring_wait_cqe(rh->ring, &cqes[0])) < 0) {
			fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
			exit(EXIT_FAILURE);
		}
		
		handle_cqe_batch(rh, cqes, 1);

		int count = io_uring_peek_batch_cqe(rh->ring, cqes, CQE_BATCH_SIZE);
		if (count == -EAGAIN) {
			/* Submit the result of handling cqe from io_uring_wait_cqe. */
			if ((ret = io_uring_submit(rh->ring)) < 0) {
				fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
				exit(EXIT_FAILURE);
			}
			continue;
		}
		if (count < 0) {
			fprintf(stderr, "io_uring_peek_batch_cqe: %s\n", strerror(-ret));
			exit(EXIT_FAILURE);
		}

		handle_cqe_batch(rh, cqes, count);

		if ((ret = io_uring_submit(rh->ring)) < 0) {
			fprintf(stderr, "io_uring_submit %s\n", strerror(-ret));
			exit(EXIT_FAILURE);
		}
	}
}

int main() {
	struct io_uring ring;
	int ret = io_uring_queue_init(QUEUE_SIZE, &ring, 0);
	if (ret < 0) {
		perror("io_uring_queue_init");
		return 1;
	}

	int server_fd = setup_server(PORT);
	printf("Server is listening\n");

	OpPool pool;
	op_pool_init(&pool);

	ClientMap clients;
	// TODO: make new constructor without cap.
	client_map_init(&clients, 1024);

	Slab slab64;
	slab_init(&slab64, 64);

	Slab slab2k;
	slab_init(&slab2k, 2048);

	RequestHandler rh = {
		.ring = &ring,
		.pool = &pool,
		.clients = &clients,
		.slab64 = &slab64,
		.slab2k = &slab2k,
		.server_fd = server_fd,
	};
	if (start_server(&rh) < 0) {
		fprintf(stderr, "failed to start server\n");
		return EXIT_FAILURE;
	}

	must_shutdown(server_fd, "server_fd shutdown");
	must_close(server_fd, "server_fd close");

	io_uring_queue_exit(&ring);
	op_pool_deinit(&pool);
	client_map_deinit(&clients);
	slab_deinit(&slab64);
	slab_deinit(&slab2k);
}
