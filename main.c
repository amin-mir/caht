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
#include "op_pool.h"
#include "slab.h"
#include "utils.h"
#include "server.h"

#define QUEUE_SIZE 4096
#define BACKLOG 10
#define PORT 8080

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
	slab_init(&slab64, BUFFER_SIZE_64B);

	Slab slab2k;
	slab_init(&slab2k, BUFFER_SIZE_2KB);

	Server srv = server_init(&ring, &clients, &slab64, &slab2k, &pool, server_fd);

	if (server_start(&srv) < 0) {
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
