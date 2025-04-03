#include "op.h"
#include "op_pool.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

char *op_type_str(enum op_type type) {
  switch (type) {
  case OP_ACCEPT:
    return "ACCEPT";
  case OP_READ:
    return "READ";
  case OP_WRITE:
    return "WRITE";
  default:
    return "UNKNOWN";
  }
}

/* op returned from this function is marked as in_use. */
struct op *op_create_accept(uint64_t id) {
  // TODO: do a single malloc that also includes the op->buf.
  struct op *op = must_malloc(sizeof(struct op));
  op->pool_id = id;
  op->type = OP_ACCEPT;
  op->client_fd = -1;
  op->buf = must_malloc(BUF_SIZE);
  op->buf_len = BUF_SIZE;
  op->processed = 0;
  memset(&op->client_addr, 0, sizeof(op->client_addr));
  op->client_addr_len = sizeof(op->client_addr);
  return op;
}

void op_destroy(struct op *op) {
  must_close(op->client_fd);
  // TODO: remove this once the whole space is allocated with a single malloc.
  free(op->buf);
  free(op);
}

void op_log_with_client_ip(struct op *op, char *msg) {
  char client_ip[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &op->client_addr.sin_addr, client_ip,
                INET_ADDRSTRLEN) == NULL) {
    fatal_error("inet_ntop");
  }
  uint16_t port = ntohs(op->client_addr.sin_port);

  uint64_t client_id = extract_client_id(op->pool_id);
  uint16_t pool_idx = extract_pool_idx(op->pool_id);
  printf("[%s:%d] fd=%d id=%lu client_id=%lu pool_idx=%u => %s\n", client_ip,
         port, op->client_fd, op->pool_id, client_id, pool_idx, msg);
}

bool op_is_incomplete(struct op *op, size_t processed) {
  size_t requested = op->buf_len - op->processed;
  return processed < requested;
}
