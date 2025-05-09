#ifndef PROT_H
#define PROT_H

#include <liburing.h>

/* Length of header which consists of len (2) + message type (1) + seqid (8) */
#define PROT_HDR_LEN 11

/* Maximum number of user IDs that server sends/receives. */
#define MAX_UIDS_PER_MSG 200

/* Username len bounds. */
#define MIN_UNAME_LEN 3
#define MAX_UNAME_LEN 15

typedef enum {
	MSGT_SERVER_ERROR,
	MSGT_SET_USERNAME,
	MSGT_SET_USERNAME_RESPONSE,
	MSGT_CREATE_GROUP,
	MSGT_CREATE_GROUP_RESONSE,
} MessageType;

/**
 * buf argument to ser_* functions MUST BE ALIGNED properly.
 *
 * buf_len argument in ser_* functions determines the total capacity of the buf
 * that we can write into. In deser_* functions, however, it tells us the len which
 * was previously read from the header of the message.
 *
 * For deser_* functions, however, buf argument DOES NOT NEED TO BE ALIGNED. We use
 * separate memcpy to copy each field individually. The buf argument must include all
 * bytes from the beginning of the message which includes the header bytes because we
 * use offsetof to access first byte of each field.
 *
 * Also deser_* functions assume there are enough bytes in the buf to parse the message.
 * deser_* functions return -1 if an error happens or 0 in case of success. The errors
 * checked by deser_* functions are related to the protocol itself e.g. conflicting len
 * values within the message. It doesn't check the business logic such as username len
 * should be within the valid range.
 *
 * Header deserialization is a special case because caller of this function will
 * check we have received enough bytes to parse the header. For other messages the
 * corresponding functions will always have buf_len as the first argument.
 */
int deser_header(const char *buf, uint16_t *len, uint8_t *msgt, uint64_t *seqid);

int deser_server_error(size_t buf_len, const char *buf, uint8_t *code);

size_t ser_server_error(size_t buf_len, char *buf, uint64_t seqid, uint8_t code);

int deser_set_username(
	size_t buf_len,
	const char *buf,
	const char **uname,
	size_t *uname_len
);

size_t ser_set_username(
	size_t buf_len, 
	char *buf, 
	uint64_t seqid, 
	size_t ulen, 
	const char *uname
);

/**
 * int deser_set_username_response(const char *buf) {}
 *
 * This function has not been implemented here because its payload is equivalent to Header.
 * Thus we only need to parse the header, and look for the correct message type and that the
 * seqid matches what was sent in the SetUsernameRequest.
 */

size_t ser_set_username_response(size_t buf_len, char *buf, uint64_t seqid);

int deser_create_group(
	size_t buf_len,
	const char *buf,
	uint8_t *uids_len,
	uint64_t *uids,        /* Caller-allocated output array. */
	const char **uids_raw, /* Ptr to the raw network uids bytes. */
	size_t *uids_raw_len
);

size_t ser_create_group(
	char *buf, 
	size_t buf_len, 
	uint64_t seqid, 
	const uint64_t *uids,
	uint8_t uids_len
);

#endif
