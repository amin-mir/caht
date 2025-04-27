#include "utils.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_SIZE 256
#define NUM_MSG 1000
#define CQE_BATCH_SIZE 10
#define MAX_NUM_MSG 10000000

void set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl(F_GETFL)");
		exit(EXIT_FAILURE);
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		perror("fcntl(F_SETFL)");
		exit(EXIT_FAILURE);
	}
}

void connect_server(struct io_uring *ring, struct sockaddr_in *server_addr,
										int client_fd) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full");
		exit(EXIT_FAILURE);
	}

	io_uring_prep_connect(sqe, client_fd, (struct sockaddr *)server_addr,
												sizeof(*server_addr));

	if (io_uring_submit(ring) < 0) {
		perror("io_uring_submit");
		exit(EXIT_FAILURE);
	}

	struct io_uring_cqe *cqe;
	if (io_uring_wait_cqe(ring, &cqe) < 0) {
		perror("io_uring_wait_cqe");
		exit(EXIT_FAILURE);
	}

	if (cqe->res < 0) {
		fprintf(stderr, "connect: %s\n", strerror(-cqe->res));
		exit(EXIT_FAILURE);
	}

	printf("Connected to server with fd: %d\n", client_fd);
	io_uring_cqe_seen(ring, cqe);
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

// TODO: seamless wait with io_uring and chaining of operations.
// void sleep_10ms() {
//	 struct timespec ts;
//	 ts.tv_sec = 0;
//	 ts.tv_nsec = 10 * 1000000L; // 10ms = 10,000,000 nanoseconds
//	 nanosleep(&ts, NULL);
// }

void run_client(struct io_uring *ring, char *send_buf, char *recv_buf,
								int client_fd, int num_messages) {
	int num_sent = 0;
	int num_recv = 0;
	struct io_uring_cqe *cqes[CQE_BATCH_SIZE];

	// if (fcntl(client_fd, F_GETFD) == -1) {
	//	 perror("client_fd is invalid");
	//	 exit(EXIT_FAILURE);
	// }

	int num_ack = 0;
	int op_type_send = 1;
	int op_type_recv = 2;
	while (num_ack < num_messages) {
		for (int i = num_sent; i < num_sent + 100 && i < num_messages; i++) {
			char *op_type_buf = send_buf + i * sizeof(int) * 2;
			char *op_data_buf = op_type_buf + sizeof(int);
			memcpy(op_type_buf, &op_type_send, sizeof(int));
			write_int_to_buffer(op_data_buf, i);

			struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
			if (sqe == NULL) {
				fprintf(stderr, "SQ is full");
				exit(EXIT_FAILURE);
			}

			io_uring_prep_send(sqe, client_fd, op_data_buf, sizeof(int), 0);
			sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
			io_uring_sqe_set_data(sqe, op_type_buf);
			// sleep_10ms();
		}

		if (io_uring_submit(ring) < 0) {
			perror("io_uring_submit");
			exit(EXIT_FAILURE);
		}

		num_sent += 100;
		if (num_sent >= num_messages) {
			break;
		}

		for (int i = num_recv; i < num_recv + 10 && i < num_messages; i++) {
			char *op_type_buf = recv_buf + i * sizeof(int) * 2;
			char *op_data_buf = op_type_buf + sizeof(int);
			memcpy(op_type_buf, &op_type_recv, sizeof(int));

			struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
			if (sqe == NULL) {
				fprintf(stderr, "SQ is full");
				exit(EXIT_FAILURE);
			}

			io_uring_prep_recv(sqe, client_fd, op_data_buf, sizeof(int), 0);
			// sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
			io_uring_sqe_set_data(sqe, op_type_buf);
			// sleep_10ms();
		}

		if (io_uring_submit(ring) < 0) {
			perror("io_uring_submit");
			exit(EXIT_FAILURE);
		}

		num_recv += 10;

		// check res >= 0 and mark seen
		int count = io_uring_peek_batch_cqe(ring, cqes, CQE_BATCH_SIZE);
		if (count == -EAGAIN) {
			continue;
		}
		if (count < 0) {
			perror("io_uring_peek_batch_cqe");
			exit(EXIT_FAILURE);
		}
		for (int i = 0; i < count; i++) {
			struct io_uring_cqe *cqe = cqes[i];
			io_uring_cqe_seen(ring, cqe);
			char *buf = io_uring_cqe_get_data(cqe);
			if (cqe->res < 0) {
				fprintf(stderr, "recv failed: %s\n", strerror(-cqe->res));
				continue;
			}
			int op_type;
			memcpy(&op_type, buf, sizeof(int));
			if (op_type == op_type_send) {
				printf("cqe for send\n");
			} else if (op_type == op_type_recv) {
				buf += sizeof(int);
				printf("recv: %d\n", read_int_from_buffer(buf));
				num_ack++;
			}
		}
	}
	printf("received %d messages from server\n", num_ack);
}

char *malloc_buf(size_t num_msg) {
	char *buf = malloc(num_msg * sizeof(int) * 2);
	if (buf == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return buf;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <SERVER_IP> <SERVER_PORT>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	const char *ip_str = argv[1];
	struct in_addr server_ip;
	if (inet_pton(AF_INET, ip_str, &server_ip) == 0) {
		fprintf(stderr, "Invalid server ip: %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	int server_port = parse_int(argv[2]);
	if (server_port < 0 || server_port > 65535) {
		fprintf(stderr, "Invalid server port: %s\n", argv[2]);
		exit(EXIT_FAILURE);
	}

	int num_messages = NUM_MSG;

	char *prefix = "--num_messages=";
	if (argc == 4) {
		printf("%s\n", argv[3]);
		if (strncmp(argv[3], prefix, strlen(prefix)) == 0) {
			num_messages = parse_int(argv[3] + strlen(prefix));
		}
	} else if (argc == 5) {
		printf("%s %s\n", argv[3], argv[4]);
		if (strncmp(argv[3], prefix, strlen(prefix) - 1) == 0) {
			num_messages = parse_int(argv[4]);
		}
	}

	if (num_messages < 0 || num_messages > MAX_NUM_MSG) {
		fprintf(stderr, "Invalid num messages: %s\n", argv[4]);
		exit(EXIT_FAILURE);
	}
	printf("num messages to send: %d\n", num_messages);

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

	char *send_buf = malloc_buf(num_messages);
	char *recv_buf = malloc_buf(num_messages);

	int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0) {
		perror("socket()");
		exit(EXIT_FAILURE);
	}
	set_nonblocking(client_fd);
	connect_server(&ring, &server_addr, client_fd);
	// run_client(&ring, send_buf, recv_buf, client_fd, num_messages);

	int count = 0;
	while (count < 5) {
		count++;
		run_client(&ring, send_buf, recv_buf, client_fd, num_messages);

		int old_fd = client_fd;

		client_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (client_fd < 0) {
			perror("socket()");
			exit(EXIT_FAILURE);
		}
		set_nonblocking(client_fd);
		connect_server(&ring, &server_addr, client_fd);

		close(old_fd);
		char *new_send_buf = malloc_buf(num_messages);
		char *new_recv_buf = malloc_buf(num_messages);

		free(send_buf);
		free(recv_buf);
		send_buf = new_send_buf;
		recv_buf = new_recv_buf;
	}
	// shutdown(client_fd, SHUT_WR);
	// close(client_fd);

	io_uring_queue_exit(&ring);
	free(send_buf);
	free(recv_buf);
}
