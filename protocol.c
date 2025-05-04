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
 * request was handled. Also if server fails to handle any of the requests,
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
#include <netinet/in.h>
#include <string.h>
#include <assert.h>

#include "protocol.h"
#include "utils.h"

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

void deser_header(const char *buf, uint16_t *len, uint8_t *msgt) {
	Header hdr;
	memcpy(&hdr, buf, sizeof(hdr));

	/**
	 * Header is a packed struct so these accesses are most likely translated
	 * to memory copy by the compiler, because accessing them directly is less
	 * efficient e.g. on x86.
	 */
	*len = ntohs(hdr.len);
	*msgt = hdr.msgt;
}

size_t ser_server_error(size_t buf_len, char *buf, uint64_t seqid, uint8_t code) {
	ServerError *se = (ServerError *)buf;
	size_t len = htons(sizeof(*se));
	assert(len <= buf_len);

	se->len = len;
	se->msgt = MSGT_SERVER_ERROR;
	se->seqid = htonll(seqid);
	se->code = code;

	return len;
}

void deser_server_error(const char *buf, uint64_t *seqid, uint8_t *code) {
	ServerError se;
	memcpy(&se, buf, sizeof(se));

	*seqid = ntohll(se.seqid);
	*code = se.code;
}

void deser_set_username_request(
	const char *buf, 
	uint64_t *seqid, 
	const char **uname, 
	size_t *uname_len
) {
	SetUsernameRequest req_hdr;
	memcpy(&req_hdr, buf, sizeof(req_hdr));

	uint16_t total_len = ntohs(req_hdr.len);
	*seqid = ntohll(req_hdr.seqid);
	*uname = buf + sizeof(req_hdr);
	*uname_len = total_len - sizeof(req_hdr);
}

size_t ser_set_username_request(
	size_t buf_len, 
	char *buf, 
	uint64_t seqid, 
	size_t uname_len, 
	const char *uname
) {
	SetUsernameRequest *req = (SetUsernameRequest *)buf;
	uint16_t len = sizeof(*req) + uname_len;
	assert(len <= buf_len);

	req->len = htons(len);
	req->msgt = MSGT_SET_USERNAME;
	req->seqid = htonll(seqid);
	memcpy(req->username, uname, uname_len);

	return len;
}

/**
 * More efficient implementation of the general form of deser function as it avoids
 * copying the header bytes.
 *
 * ```
 * void deser_set_username_response(const char *buf, uint64_t *seqid) {
 *   SetUsernameResponse resp;
 *   memcpy(&resp, buf, sizeof(resp));
 *   seqid = ntohll(resp.seqid);
 * }
 * ```
 */
void deser_set_username_response(const char *buf, uint64_t *seqid) {
    const SetUsernameResponse *resp = (const SetUsernameResponse *)buf;
    uint64_t raw_seqid;

	/**
	 * dereferencing the fields directly is unsafe because buf may not be
	 * properly aligned:
	 *
	 * uint64_t x = resp->seqid;
	 *
	 * But below we're only taking the address of a field and memcpy only
	 * cares about copying bytes.
	 */

    memcpy(&raw_seqid, &resp->seqid, sizeof(raw_seqid));
    *seqid = ntohll(raw_seqid);
}

size_t ser_set_username_response(size_t buf_len, char *buf, uint64_t seqid) {
	SetUsernameResponse *resp = (SetUsernameResponse *)buf;
	uint64_t len = sizeof(*resp);
	assert(len <= buf_len);

	resp->len = htons(len);
	resp->msgt = MSGT_SET_USERNAME_RESPONSE;
	resp->seqid = htonll(seqid);

	return len;
}
