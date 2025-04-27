#include <liburing.h>

#include "utils.h"
#include "op.h"
#include "client_map.h"
#include "op_pool.h"
#include "slab.h"

/* Message type can be read from the request at this offset. */
#define PROT_MSGT_OFFT 2

/* Length of header which consists of len (2) + message type (1) */
#define PROT_HDR_LEN 3

/**************** WIRE PROTOCOL MESSAGES - START ****************/
#pragma pack(push, 1)

typedef struct {
	uint16_t len;
	uint8_t msgt;
} Header;

typedef struct {
    uint16_t len;
    uint8_t msgt;
    uint64_t seqid;
    uint8_t code;
} ServerError;

typedef struct {
	uint16_t len;
	uint8_t msgt;
	uint64_t seqid;
	uint8_t username[];
} SetUsernameRequest;

typedef struct {
	uint16_t len;
	uint8_t msgt;
	uint64_t seqid;
} SetUsernameResponse;

#pragma pack(pop)
/**************** WIRE PROTOCOL MESSAGES - END ****************/

typedef enum {
	MSGT_SERVER_ERROR,
	MSGT_SET_USERNAME,
	MSGT_SET_USERNAME_RESPONSE,
} MessageType;

typedef enum {
	HREQ_SUCCESS,
	HREQ_INVALID_MSG_TYPE,
	HREQ_INVALID_MSG_LEN,
	HREQ_INVALID_USERNAME,
	HREQ_FAILURE,
} HandleRequestResult;

typedef struct {
	struct io_uring *ring; 
	ClientMap *clients;
	Slab *slab64;
	Slab *slab2k;
	OpPool *pool;
	int server_fd;
} RequestHandler;

RequestHandler rh_init(
	struct io_uring *ring, 
	ClientMap *clients,
	Slab *slab64,
	Slab *slab2k,
	OpPool *pool,
	int server_fd
);

void rh_add_accept(RequestHandler *rh, uint64_t client_id);
void rh_add_recv(RequestHandler *rh, Operation *op, int client_fd);
void rh_resume_recv(RequestHandler *rh, Operation *op, size_t bytes_read);

void rh_add_send(
	RequestHandler *rh,
	void *resp,
	int client_fd,
	uint64_t client_id,
	size_t num_bytes
);

void rh_resume_send(RequestHandler *rh, Operation *op, size_t processed);

int rh_handle(
	RequestHandler *rh,
	ClientInfo *info,
	Operation *req_op,
	char *req_buf,
	size_t req_len
);

void ser_set_username_request(
	SetUsernameRequest *req,
	uint64_t seqid,
	size_t ulen,
	char username[ulen]
);

void deser_set_username_response(const char *buf, uint64_t *seqid);
