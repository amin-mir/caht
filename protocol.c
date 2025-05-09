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
 * len = 11
 *
 *
 *** GET_USERNAME_RESPONSE
 * 
 * <len:2> <msgt:1> <count:4> [<usrlen:1> <username>]*count
 *
 * 7 <= len <= 2048
 *
 * `count` is the number of usernames that follows. For each username, first read
 * its length and then read `usrlen` bytes. Client should add the terminating NULL char
 * when storing it in memory.
 *
 * In case gid is invalid, count would be 0.
 *
 * We assume that we can send all usernames in a single message for now.
 * Splitting the response into several messages will happen in the future.
 *
 *
 *** CREATE_GROUP
 * <len:2> <msgt:1> <uids_len:1> [<uid:8>]*count
 * 12 <= len <= 2044
 * 1 <= count <= 255
 *
 * Server creates a group with the provided user IDs and the user who issues this request.
 * Server will return the group ID as response in CREATE_GROUP_RESPONSE to the issuer.
 * Server will also send the group ID to all other users in JOINED_GROUP.
 * Limit of the number of user IDs which can be included in this message is 200 because
 * send buffers are 2KiB which can fit around that many uids.
 *
 *
 *** CREATE_GROUP_RESPONSE
 * <len:2> <msgt:1> <gid:8>
 * len = 11
 *
 *
 *** JOINED_GROUP
 * <len:2> <msgt:1> <uid:8> <gid:8> <count:1> [<uid:8>]*count
 * 28 <= len <= 2048
 *
 * Server will send this message to all clients who were added to a group by another client
 * that just created a group.
 * The uids can just be directly copied from the originating CREATE_GROUP message.
 *
 *
 *** ADD_TO_GROUP
 * <len:2> <msgt:1> <gid:8> <count:1> [<uid:8>]*count
 * len = 11
 * msgt = 4
 *
 *
 *** ADD_TO_GROUP_RESPONSE
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
#include <stddef.h>

#include "protocol.h"
#include "utils.h"

/**************** WIRE PROTOCOL MESSAGES - START ****************/
#pragma pack(push, 1)

typedef struct {
	uint16_t len;
	uint8_t msgt;
	uint64_t seqid;
} Header;

typedef struct {
	Header hdr;
    uint8_t code;
} ServerError;

typedef struct {
	Header hdr;
	uint8_t username[];
} SetUsernameRequest;

typedef struct {
	Header hdr;
} SetUsernameResponse;

typedef struct {
	Header hdr;
	uint8_t uids_len;
	uint64_t uids[];
} CreateGroup;

#pragma pack(pop)
/**************** WIRE PROTOCOL MESSAGES - END ****************/

int deser_header(const char *buf, uint16_t *len, uint8_t *msgt, uint64_t *seqid) {
	uint16_t net_len;
	memcpy(&net_len, buf, sizeof(net_len));
	*len = ntohs(net_len);

	*msgt = *(const uint8_t *)(buf + offsetof(Header, msgt));

	uint64_t net_seqid;
	memcpy(&net_seqid, buf + offsetof(Header, seqid), sizeof(net_seqid));
	*seqid = ntohll(net_seqid);

	return 0;
}

int deser_server_error(size_t buf_len, const char *buf, uint8_t *code) {
	if (buf_len != sizeof(ServerError)) return -1;
	*code = *(const uint8_t *)(buf + offsetof(ServerError, code));
	return 0;
}

size_t ser_server_error(size_t buf_len, char *buf, uint64_t seqid, uint8_t code) {
	ServerError *se = (ServerError *)buf;
	uint16_t len = sizeof(*se);
	assert(len <= buf_len);

	se->hdr.len = htons(len);
	se->hdr.msgt = MSGT_SERVER_ERROR;
	se->hdr.seqid = htonll(seqid);
	se->code = code;

	return len;
}

int deser_set_username(
	size_t buf_len,
	const char *buf,
	const char **uname,
	size_t *uname_len
) {
	/**
	 * We don't need to check buf_len for this message type using the following if because
	 * uname_len will be set to 0 if client doesn't send any bytes for username. The caller
	 * will check the uname_len to be within acceptable bounds.
	 *
	 * if (buf_len != sizeof(SetUsernameRequest)) return -1;
	 */
	*uname = buf + offsetof(SetUsernameRequest, username);
	*uname_len = buf_len - sizeof(SetUsernameRequest);
	return 0;
}

size_t ser_set_username(
	size_t buf_len,
	char *buf,
	uint64_t seqid,
	size_t uname_len,
	const char *uname
) {
	SetUsernameRequest *req = (SetUsernameRequest *)buf;
	uint16_t len = sizeof(*req) + uname_len;
	assert(len <= buf_len);

	req->hdr.len = htons(len);
	req->hdr.msgt = MSGT_SET_USERNAME;
	req->hdr.seqid = htonll(seqid);
	memcpy(req->username, uname, uname_len);

	return len;
}

size_t ser_set_username_response(size_t buf_len, char *buf, uint64_t seqid) {
	SetUsernameResponse *resp = (SetUsernameResponse *)buf;
	uint64_t len = sizeof(*resp);
	assert(len <= buf_len);

	resp->hdr.len = htons(len);
	resp->hdr.msgt = MSGT_SET_USERNAME_RESPONSE;
	resp->hdr.seqid = htonll(seqid);

	return len;
}

int deser_create_group(
	size_t buf_len,
	const char *buf,
	uint8_t *uids_len,
	uint64_t *uids,
	const char **uids_raw,
	size_t *uids_raw_len
) {
	/**
	 * In case client doesn't send any uids, the uids_len will be set to 0 and the
	 * caller can validate that. Thus we only check there's enough bytes for the fixed
	 * part here.
	 */
	if (buf_len < sizeof(CreateGroup)) return -1;

	*uids_len = *(uint8_t *)(buf + offsetof(CreateGroup, uids_len));

	/* Ensure uids has enough space. MAX_UIDS_PER_MSG is max capacity of uids array. */
	if (*uids_len > MAX_UIDS_PER_MSG) return -1;

	/* Ensure client didn't send a wrong value for the count of uids. */
	size_t expected_uids_len = *uids_len * sizeof(uint64_t);
	if (expected_uids_len != (buf_len - sizeof(CreateGroup))) return -1;

	const char *uids_base = buf + offsetof(CreateGroup, uids);
	*uids_raw = uids_base;
	*uids_raw_len = expected_uids_len;

	for (uint8_t i = 0; i < *uids_len; ++i) {
		uint64_t net_uid;
		/* Relying on the compiler to optimize this pointer arithmetic. */
		memcpy(&net_uid, uids_base + i * sizeof(uint64_t), sizeof(net_uid));
		uids[i] = ntohll(net_uid);
	}

	return 0;
}

size_t ser_create_group(
	char *buf, 
	size_t buf_len, 
	uint64_t seqid, 
	const uint64_t *uids,
	uint8_t uids_len
) {
	assert(uids_len <= MAX_UIDS_PER_MSG);

	CreateGroup *cg = (CreateGroup *)buf;
	/* len is size_t to prevent overflows. */
	size_t len = sizeof(*cg) + uids_len * sizeof(uint64_t);
	assert(len <= buf_len);

	cg->hdr.len = htons((uint16_t)len);
	cg->hdr.msgt = MSGT_CREATE_GROUP;
	cg->hdr.seqid = htonll(seqid);
	cg->uids_len = uids_len;

	char *uids_base = buf + sizeof(CreateGroup);
	for (uint8_t i = 0; i < uids_len; ++i) {
		uint64_t net_uid = htonll(uids[i]);
		memcpy(uids_base + i * sizeof(uint64_t), &net_uid, sizeof(net_uid));
	}

	return len;
}
