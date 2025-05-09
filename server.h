#ifndef SERVER_H
#define SERVER_H

#include <liburing.h>

#include "utils.h"
#include "op.h"
#include "client_map.h"
#include "op_pool.h"
#include "slab.h"

#define BUFFER_SIZE_64B 64
#define BUFFER_SIZE_2KB 2048

typedef enum {
	CODE_SUCCESS,
	CODE_INVALID_MSG_TYPE,
	CODE_INVALID_MSG_LEN,
	CODE_INVALID_USERNAME,
	CODE_FAILURE,
} ResponseCode;

typedef struct {
	struct io_uring *ring; 
	ClientMap *clients;
	Slab *slab64;
	Slab *slab2k;
	OpPool *pool;
	int server_fd;
} Server;

Server server_init(struct io_uring *ring, ClientMap *clients, Slab *slab64,
				   Slab *slab2k, OpPool *pool, int server_fd);

int server_start(Server *srv);

#endif
