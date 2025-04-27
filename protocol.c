/**
 * General Design
 *
 * All messages are prefixed with a 2-byte len part.
 *
 * `len` has a maximum of 2048 bytes indicating the upper bound for length of
 * each message (including 2 bytes for len and 1 for message type).
 *
 * All IDs are 8 bytes including group ID, client ID, message ID.
 *
 * Clients send an 8-byte seq ID in their requests which the server will then
 * use in the response. This can help the client, for instance, distinguish which
 * `send` message was handled. Also if server fails to handle any of the requests,
 * it will issue a SERVER_ERROR that matches seq ID of the request and a code for
 * the reason for failure. Clients may want to increase their seq ID each time they
 * make a request to the server.
 *
 * Server keeps track of a monotonically increasing message id per group. This is
 * different than seq ID in that it is per group, not per client. If 5 clients send
 * 5 messages to the group, the group ID will be incremented by 5 in the end, so a
 * client cannot assume it to have sequential properties. 
 *
 * All messaging happens through the use of groups. Even direct messages between two
 * clients happen through a group consisting of their client IDs.
 *
 * Here's a sample scenario in which a client sends a message to group consisting of
 * 10 members. Upon receiving the message, the server will attempt to send it to all
 * members of the group except the sender. Sender will receive a separate 
 * SEND_TO_GROUP_RESPONSE with the provided seq ID.
 *
 *
 *** SERVER_ERROR
 *
 * <len:2> <msgt:1> <seqid:8> <code:1>
 * len = 12
 *
 *
 *** SET_USERNAME
 *
 * <len:2> <msgt:1> <seqid:8> <username:15>
 *
 * Server allocates 16 bytes for each username and the last byte is dedicated to
 * the terminating null character, thus username may have a max length of 15 bytes.
 * Server will add the '\0' in the end. Username has a min length of 3.
 *
 * 6 <= len <= 18
 *
 *
 *** SET_USERNAME_RESPONSE
 * 
 * <len:2> <msgt:1> <seqid:8>
 * len = 11
 *
 *
 *** GET_USERNAMES
 * 
 * <len:2> <msgt:1> <gid:8>
 *
 *
 *** GET_USERNAME_RESPONSE
 * 
 * <len:2> <msgt:1> <count:4> [<usrlen:1> <username>]*count
 *
 * 7 <= len <= 2048
 *
 * `count` is the number of usernames that follows. For each username, first read
 * its length and then read `usrlen` bytes. Client should add the terminating NULL char.
 *
 * In case gid is invalid, count would be 0.
 *
 * We assume that we can send all usernames in a single message for now.
 * Splitting the response into several messages will happen in the future.
 *
 *
 *** CREATE_GROUP
 * <len:2> <msgt:1>
 * len = 3
 * msgt = 2
 * server creates a group and returns an id.
 *
 *
 *** CREATE_GROUP_RESPONSE
 * <len:2> <msgt:1> <gid:8>
 * len = 11
 * msgt = 3
 *
 *
 *** JOIN_GROUP
 * <len:2> <msgt:1> <gid:8>
 * len = 11
 * msgt = 4
 *
 *
 *** JOIN_GROUP_RESPONSE
 * <len:2> <msgt:1> <res:1>
 * len = 4
 * msgt = 5
 * res is 0 (failure) or 1 (success).
 *
 *
 *** SEND_TO_GROUP
 * <len:2> <msgt:1> <gid:8> <seqid:8> <msg>
 * 20 <= len <= 2048
 * msgt = 6
 * 
 *
 *** SEND_TO_GROUP_RESPONSE
 * <len:2> <msgt:1> <gid:8> <msgid:8> <seqid:8>
 * len = 27
 * msgt = 7
 *
 *
 *** RECEIVE_FROM_GROUP
 * <len:2> <msgt:1> <gid:8> <msgid:8> <msg>
 * 22 <= len <= 2048
 * msgt = 8
 *
 */

#include <liburing.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>

#include "protocol.h"

bool username_valid(size_t len, char username[len]) {
	for (size_t i = 0; i < len; i++) {
		char c = username[i];
		if (!isalnum(c)) return false;
	}
	return true;
}

RequestHandler rh_init(
	struct io_uring *ring, 
	ClientMap *clients,
	Slab *slab64,
	Slab *slab2k,
	OpPool *pool,
	int server_fd
) {
	return (RequestHandler){
		.ring = ring,
		.clients = clients,
		.slab64 = slab64,
		.slab2k = slab2k,
		.pool = pool,
		.server_fd = server_fd,
	};
}

void rh_add_accept(RequestHandler *rh, uint64_t client_id) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(rh->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	Operation *op = op_pool_new_entry(rh->pool);
	assert(op != NULL);

	/**
	 * op->pool_id should not be modified.
	 *
	 * Directly getting a buffer from slab2k instead of calling acquire_op_buf because
	 * the buffer will be used for recv operations and we want it to be as large as possible.
	 */
	op->buf = slab_get(rh->slab2k);
	assert(op->buf != NULL);
	op->buf_len = 0;
	op->buf_cap = slab_buf_cap(rh->slab2k);
	op->client_id = client_id;
	op->processed = 0;
	op->client_fd = -1;
	op->type = OP_ACCEPT;

	ClientInfo *info;
	client_map_new_entry(rh->clients, client_id, &info);
	memset(&info->client_addr, 0, sizeof(info->client_addr));
	info->client_addr_len = sizeof(info->client_addr);

	printf("ADD ACCEPT client_id=%lu\n", client_id);

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	/* Setting SOCK_NONBLOCK saves us extra calls to fcntl. */
	io_uring_prep_accept(
		sqe, 
		rh->server_fd, 
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
void rh_add_recv(RequestHandler *rh, Operation *op, int client_fd) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(rh->ring);
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

void rh_resume_recv(RequestHandler *rh, Operation *op, size_t bytes_read) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(rh->ring);
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

void acquire_send_buf(RequestHandler *rh, size_t len, Operation *op) {
	Slab *s = rh->slab64;
	if (len > 64) {
		s = rh->slab2k;
	}
	op->buf = slab_get(s);
	assert(op->buf != NULL);
	op->buf_cap = slab_buf_cap(s);
	op->buf_len = len;
}

void rh_add_send(
	RequestHandler *rh, 
	void *resp, 
	int client_fd, 
	uint64_t client_id,
	size_t num_bytes
) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(rh->ring);
	if (sqe == NULL) {
		fprintf(stderr, "SQ is full\n");
		exit(EXIT_FAILURE);
	}

	Operation *op = op_pool_new_entry(rh->pool); 
	assert(op != NULL);

	/* op->pool_id should not be modified. */
	acquire_send_buf(rh, num_bytes, op);
	memcpy(op->buf, &resp, num_bytes);
	op->client_id = client_id;
	op->processed = 0;
	op->client_fd = client_fd;
	op->type = OP_WRITE;

	io_uring_sqe_set_data(sqe, (void *)op->pool_id);
	io_uring_prep_send(sqe, op->client_fd, op->buf, op->buf_len, 0);
}

void rh_resume_send(RequestHandler *rh, Operation *op, size_t processed) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(rh->ring);
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

/******************** SER/DESER - START ********************/
void send_server_error(
	RequestHandler *rh,
	int client_fd,
	uint64_t seqid,
	uint64_t client_id,
	uint8_t code
) {
	ServerError resp;
	resp.len = htons(sizeof(resp));
	resp.msgt = MSGT_SERVER_ERROR;
	resp.seqid = htonll(seqid);
	resp.code = code;

	rh_add_send(rh, &resp, client_fd, client_id, sizeof(resp));
}

/**
 * We cannot safely declare a flexible array member struct (SetUsernameRequest)
 * on the stack like this because sizeof(req) only includes the fixed part:
 *
 * ```
 * SetUsernameRequest req;
 * ```
 *
 * Memory should be dynamically malloc'd before calling this function:
 *
 * ```
 * size_t len = sizeof(SetUsernameRequest) + ulen;
 * SetUsernameRequest *req = malloc(len);
 * ser_set_username_request(req, seqid, ulen, username);
 * ```
 *
 * Or allocated on the stack:
 * uint8_t buf[sizeof(SetUsernameRequest) + MAX_USERNAME_LEN];
 * SetUsernameRequest *req = (SetUsernameRequest *)buf;
 * ser_set_username_request(req, seqid, ulen, username);
 */
void ser_set_username_request(
	SetUsernameRequest *req,
	uint64_t seqid,
	size_t ulen,
	char username[ulen]
) {
	uint16_t len = sizeof(*req) + ulen;

	req->len = htons(len);
	req->msgt = MSGT_SET_USERNAME;
	req->seqid = htonll(seqid);
	memcpy(req->username, username, ulen);
}

void send_set_username_response(
	RequestHandler *rh,
	int client_fd,
	uint64_t seqid,
	uint64_t client_id
) {
	SetUsernameResponse resp;
	resp.len = htons(sizeof(resp));
	resp.msgt = MSGT_SET_USERNAME_RESPONSE;
	resp.seqid = htonll(seqid);

	rh_add_send(rh, &resp, client_fd, client_id, sizeof(resp));
}

void deser_header(const char *buf, uint16_t *len, uint8_t *msgt) {
	Header hdr;
	memcpy(&hdr, buf, sizeof(hdr));
	*len = hdr.len;
	*msgt = hdr.msgt;
}

/**
 * ```
 * void deser_set_username_response(const char *buf, uint64_t *seqid) {
 *  	SetUsernameResponse resp;
 *  	memcpy(&resp, buf, sizeof(resp));
 *  	seqid = ntohll(resp.seqid);
 * }
 * ```
 *
 * More efficient than the commented function above because it avoids
 * copying the header bytes.
 */
void deser_set_username_response(const char *buf, uint64_t *seqid) {
    const SetUsernameResponse *resp = (const SetUsernameResponse *)buf;
    uint64_t raw_seqid;

	/**
	 * dereferencing the fields directly is unsafe:
	 *
	 * uint64_t x = resp->seqid;
	 *
	 * But below we're only taking the address of a field and memcpy only
	 * cares about copying bytes.
	 */

    memcpy(&raw_seqid, &resp->seqid, sizeof(raw_seqid));
    *seqid = ntohll(raw_seqid);
}
/******************** SER/DESER - END ********************/

/**
 * rh_handle will add sqes but will not submit. We assume ring will have
 * enough room for the resulting sqes. However, for massive groups the job has
 * to be handles in several stages in order to avoid overflowing the sqe buffer.
 *
 * req_len is essentially the prefix of the each message already parsed by the
 * caller of this function. We assume req_len >= 3 and req should contain all bytes 
 * necessary for parsing a request.
 */
int rh_handle(
	RequestHandler *rh,
	ClientInfo *info,
	Operation *req_op,
	char *req_buf,
	size_t req_len
) {
	int client_fd = req_op->client_fd;
	uint64_t client_id = req_op->client_id;

	char msg_type = req_buf[PROT_MSGT_OFFT];
	size_t remaining = req_len - PROT_HDR_LEN;
	switch (msg_type) {
		case MSGT_SET_USERNAME: {
			/* Network byte order to host byte order. */
			SetUsernameRequest *r = (SetUsernameRequest *)(req_buf);
			uint64_t seqid = ntohll(r->seqid);
			size_t username_len = req_len - sizeof(SetUsernameRequest);

			/* Ensure username len is in range [3, 15]. */
			if (username_len < 3 || username_len > 15) {
				uint8_t code = HREQ_INVALID_MSG_LEN;
				send_server_error(rh, client_fd, seqid, client_id, code);
				return 0;
			}

			/* Ensure username chars are valid. */
			if (!username_valid(username_len, req_buf)) {
				uint8_t code = HREQ_INVALID_USERNAME;
				send_server_error(rh, client_fd, seqid, client_id, code);
				return 0;
			}
			
			memcpy(info->username, r->username, username_len);
			info->username[remaining] = '\0';

			send_set_username_response(rh, client_fd, seqid, client_id);
			return 0;
		}
		default: {
			return -1;
		}
	};
}
