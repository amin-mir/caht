#include <assert.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"
#include "protocol.h"

#define QUEUE_SIZE 256
#define CQE_BATCH_SIZE 10
#define BUFFER_SIZE 2048

#define TYPE_SEND 1
#define TYPE_RECV 2

typedef enum {
	OP_TYPE_SEND,
	OP_TYPE_RECV
} OperationType;

typedef struct {
	OperationType op_type;
	size_t        buf_len;
	char          buf[];
} ClientOperation;

void connect_server(
	struct io_uring *ring, 
	struct sockaddr_in *server_addr,
	int client_fd
) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full");
		exit(EXIT_FAILURE);
	}

	io_uring_prep_connect(
		sqe, client_fd,
		(struct sockaddr *)server_addr,
		sizeof(*server_addr)
	);

	int ret;
	if ((ret = io_uring_submit(ring)) < 0) {
		fprintf(stderr, "io_uring_submit %s\n", strerror(-ret));
		exit(EXIT_FAILURE);
	}

	struct io_uring_cqe *cqe;
	if ((ret = io_uring_wait_cqe(ring, &cqe)) < 0) {
		fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
		exit(EXIT_FAILURE);
	}

	if (cqe->res < 0) {
		fprintf(stderr, "connect: %s\n", strerror(-cqe->res));
		exit(EXIT_FAILURE);
	}
	io_uring_cqe_seen(ring, cqe);

	printf("Connected to server with fd: %d\n", client_fd);
}

int parse_int(const char *str) {
	char *end_ptr;
	int res = strtol(str, &end_ptr, 10);
	if (*end_ptr != '\0') {
		fprintf(stderr, "Invalid num messages: %s\n", str);
		exit(EXIT_FAILURE);
	}
	return res;
}

void run_client(struct io_uring *ring, int client_fd) {
	struct io_uring_cqe *cqes[CQE_BATCH_SIZE];

	/* Send SetUsernameRequest to server. */
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full");
		exit(EXIT_FAILURE);
	}

	ClientOperation *op_send = must_malloc(
		sizeof(ClientOperation) + BUFFER_SIZE, 
		"malloc send operation"
	);
	op_send->op_type = OP_TYPE_SEND;
	op_send->buf_len = BUFFER_SIZE;

	uint64_t req_seqid = 1;
	char *username = "jojo";
	size_t ulen = strlen(username);
	size_t req_len = ser_set_username_request(
		BUFFER_SIZE, 
		op_send->buf, 
		req_seqid, 
		ulen, 
		username
	);

	io_uring_prep_send(sqe, client_fd, op_send->buf, req_len, 0);
	// sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
	io_uring_sqe_set_data(sqe, op_send);

	if (io_uring_submit(ring) < 0) {
		perror("io_uring_submit");
		exit(EXIT_FAILURE);
	}

	/* Prepare to read SetUsernameResponse from server. */
	sqe = io_uring_get_sqe(ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full");
		exit(EXIT_FAILURE);
	}

	ClientOperation *op_recv = must_malloc(
		sizeof(ClientOperation) + BUFFER_SIZE, 
		"malloc send operation"
	);
	op_recv->op_type = OP_TYPE_RECV;
	op_recv->buf_len = BUFFER_SIZE;

	io_uring_prep_recv(sqe, client_fd, op_recv->buf, op_recv->buf_len, 0);
	io_uring_sqe_set_data(sqe, op_recv);
	// sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;

	if (io_uring_submit(ring) < 0) {
		perror("io_uring_submit");
		exit(EXIT_FAILURE);
	}

	struct io_uring_cqe *cqe;
	for (int i = 0; i < 2; i++) {
		int ret = io_uring_wait_cqe(ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "io_uring_wait_cqe: %s", strerror(-ret));
			exit(EXIT_FAILURE);
		}

		/**
		 * Save the necessary data into local variables and do not access cqe after
		 * calling io_uring_cqe_seen.
		 */
		int cqe_res = cqe->res;
		ClientOperation *op = io_uring_cqe_get_data(cqe);
		io_uring_cqe_seen(ring, cqe);

		if (cqe_res < 0) {
			fprintf(stderr, "operation failed: %s\n", strerror(-cqe->res));
			continue;
		}

		switch (op->op_type) {
			case OP_TYPE_SEND: {
				printf("send successful.\n");
				break;
			}
			case OP_TYPE_RECV: {
				uint8_t msgt;
				uint16_t len;
				deser_header(op->buf, &len, &msgt);
				printf("message type: %d len: %d\n", msgt, len);

				// uint8_t code;
				// uint64_t seqid;
				// deser_server_error(op->buf, &seqid, &code);
				// printf("seqid: %lu code: %d\n", seqid, code);

				assert(msgt == MSGT_SET_USERNAME_RESPONSE);

				uint64_t ack_seqid;
				deser_set_username_response(op->buf, &ack_seqid);
				assert(req_seqid == ack_seqid);
				break;
			}
			default: {
				fprintf(stderr, "invalid op type: %d\n", op->op_type);
				exit(EXIT_FAILURE);
			}
		}
	}
}

int main(int argc, char *argv[]) {
	char *ip_str;
	char *port_str;
	if (argc == 3) {
		ip_str = argv[1];
		port_str = argv[2];
	} else {
		ip_str = "127.0.0.1";
		port_str = "8080";
	}

	struct in_addr server_ip;
	if (inet_pton(AF_INET, ip_str, &server_ip) == 0) {
		fprintf(stderr, "Invalid server ip: %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	int server_port = parse_int(port_str);
	if (server_port < 0 || server_port > 65535) {
		fprintf(stderr, "Invalid server port: %s\n", argv[2]);
		exit(EXIT_FAILURE);
	}

	struct io_uring ring;
	struct sockaddr_in server_addr = {
			.sin_family = AF_INET,
			.sin_addr = server_ip,
			.sin_port = htons(server_port),
	};

	int ret = io_uring_queue_init(QUEUE_SIZE, &ring, 0);
	if (ret < 0) {
		perror("io_uring_queue_init");
		return EXIT_FAILURE;
	}

	int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0) {
		perror("socket()");
		exit(EXIT_FAILURE);
	}
	set_nonblocking(client_fd);

	connect_server(&ring, &server_addr, client_fd);
	run_client(&ring, client_fd);

	must_shutdown(client_fd, "client_fd shutdown");
	must_close(client_fd, "client_fd close");
	io_uring_queue_exit(&ring);
}
