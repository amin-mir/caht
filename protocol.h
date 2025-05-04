#include <liburing.h>

/* Message type can be read from the request at this offset. */
#define PROT_MSGT_OFFT 2

/* Length of header which consists of len (2) + message type (1) */
#define PROT_HDR_LEN 3

typedef enum {
	MSGT_SERVER_ERROR,
	MSGT_SET_USERNAME,
	MSGT_SET_USERNAME_RESPONSE,
} MessageType;

/**
 * buf argument to ser_* functions must be aligned properly.
 *
 * For deser_* functions, however, buf argument doesn't need to be aligned. The fixed
 * part of the function is copied to an aligned location on the stack before starting
 * to parse the individual fields.
 *
 * Also deser_* functions assume there are enough bytes in the buf to parse the message.
 */
void deser_header(const char *buf, uint16_t *len, uint8_t *msgt);

void deser_server_error(const char *buf, uint64_t *seqid, uint8_t *code);
size_t ser_server_error(size_t buf_len, char *buf, uint64_t seqid, uint8_t code);

void deser_set_username_request(const char *buf, uint64_t *seqid, const char **uname, 
								size_t *uname_len);
size_t ser_set_username_request(size_t buf_len, char *buf, uint64_t seqid, size_t ulen,
								const char *uname);

void deser_set_username_response(const char *buf, uint64_t *seqid);
size_t ser_set_username_response(size_t buf_len, char *buf, uint64_t seqid);
