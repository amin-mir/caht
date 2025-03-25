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

#include "op.h"
#include "pool.h"
#include "utils.h"

#define QUEUE_SIZE 4096
#define BACKLOG 10
#define PORT 8080

int setup_server(int port) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1)
    fatal_error("socket()");

  int enable = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    fatal_error("setsockopt(SO_REUSEADDR)");

  int flags = fcntl(server_fd, F_GETFL, 0);
  if (flags == -1)
    fatal_error("fcntl(F_GETFL)");

  if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    fatal_error("fcntl(F_SETFL, O_NONBLOCK)");

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int ret =
      bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

  if (ret == -1)
    fatal_error("bind");

  if (listen(server_fd, BACKLOG) == -1)
    fatal_error("listen");

  return server_fd;
}

/* Returns -1 when SQ is full or there are no free entries in ops pool.
 * In case of errors, Caller doesn't need to clean up anything as no memory will
 * be allocated.
 *
 * Returns 0 in case of success and caller must return the entry back to the ops
 * pool upon client disconnection.
 */
int add_accept(struct io_uring *ring, int server_fd) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (sqe == NULL) {
    fprintf(stderr, "SQ is full\n");
    return -1;
  }

  struct op *aop = pool_pick_free();
  if (aop == NULL) {
    fprintf(stderr, "serving max number of clients\n");
    return -1;
  }

  /* aop->buf:      is already allocated.
   * op->buf_len:   is set by recv/send.
   * op->processed: is set by recv/send.
   */
  aop->type = OP_ACCEPT;
  aop->client_fd = -1;
  memset(&aop->client_addr, 0, sizeof(aop->client_addr));
  aop->client_addr_len = sizeof(aop->client_addr);

  // uint64_t client_id = extract_client_id(aop->pool_id);
  // uint16_t pool_idx = extract_pool_idx(aop->pool_id);
  // printf("ADD ACCEPT id=%lu client_id=%lu pool_idx=%d\n", aop->pool_id,
  //        client_id, pool_idx);

  io_uring_sqe_set_data(sqe, (void *)aop->pool_id);

  /* Setting SOCK_NONBLOCK saves us extra calls to fcntl. */
  io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)&aop->client_addr,
                       &aop->client_addr_len, SOCK_NONBLOCK);
  return 0;
}

/* fd is only necessary the first time add_read is called after accept returns a
 * new client fd. The next times it is called, fd is already correctly set in
 * the op and can be passed to this function as is.
 */
int add_read(struct io_uring *ring, struct op *rop, int fd) {
  if (fcntl(fd, F_GETFD) == -1) {
    printf("skip add_read client fd=%d is closed\n", fd);
    return 0;
  }
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (sqe == NULL) {
    fprintf(stderr, "SQ is full\n");
    return -1;
  }

  /* rop->buf:        is already allocated
   * client_addr:     is set after accept
   * client_addr_len: is set after accept
   */
  rop->type = OP_READ;
  rop->client_fd = fd;
  rop->buf_len = BUF_SIZE;
  rop->processed = 0;

  io_uring_sqe_set_data(sqe, (void *)rop->pool_id);

  io_uring_prep_recv(sqe, rop->client_fd, rop->buf, rop->buf_len, 0);
  return 0;
}

int add_write(struct io_uring *ring, struct op *wop, size_t num_bytes) {
  if (fcntl(wop->client_fd, F_GETFD) == -1) {
    printf("skip add_write client fd=%d is closed\n", wop->client_fd);
    return 0;
  }
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (sqe == NULL) {
    fprintf(stderr, "SQ is full\n");
    return -1;
  }

  /* client_fd:       is already set from previous operations
   * buf:             is already set from previous operations
   * client_addr:     is set after accept
   * client_addr_len: is set after accept
   */
  wop->type = OP_WRITE;
  wop->buf_len = num_bytes;
  wop->processed = 0;

  io_uring_sqe_set_data(sqe, (void *)wop->pool_id);

  io_uring_prep_send(sqe, wop->client_fd, wop->buf, wop->buf_len, 0);
  return 0;
}

int add_short_write(struct io_uring *ring, struct op *wop, size_t processed) {
  if (fcntl(wop->client_fd, F_GETFD) == -1) {
    printf("skip add_short_write client fd=%d is closed\n", wop->client_fd);
    return 0;
  }

  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (sqe == NULL) {
    fprintf(stderr, "SQ is full\n");
    return -1;
  }

  wop->type = OP_WRITE;
  wop->processed += processed;
  /* client_fd:       is set after accept
   * buf:             is allocated when fetched from pool
   * buf_len:         is set from the previous add_write
   * client_addr:     is set after accept
   * client_addr_len: is set after accept
   */

  io_uring_sqe_set_data(sqe, (void *)wop->pool_id);

  char *buf = wop->buf + wop->processed;
  size_t len = wop->buf_len - wop->processed;
  io_uring_prep_send(sqe, wop->client_fd, buf, len, 0);
  return 0;
}

// int add_read_with_timeout(struct io_uring *ring, struct op *read_op, int
// timeout_ms) {
//     struct io_uring_sqe *sqe_read = io_uring_get_sqe(ring);
//     if (!sqe_read) {
//         fprintf(stderr, "SQ is full\n");
//         return -1;
//     }
//
//     read_op->type = OP_READ;
//     read_op->buf = must_malloc(BUF_SIZE);
//     read_op->buflen = BUF_SIZE;
//     io_uring_sqe_set_data(sqe_read, read_op);
//     io_uring_prep_recv(sqe_read, read_op->client_fd, read_op->buf,
//     read_op->buflen, 0);
//
//     /* Fetch another SQE for the timeout */
//     struct io_uring_sqe *sqe_timeout = io_uring_get_sqe(ring);
//     if (!sqe_timeout) {
//         fprintf(stderr, "SQ is full\n");
//         return -1;
//     }
//
//     struct __kernel_timespec ts;
//     ts.tv_sec = timeout_ms / 1000;
//     ts.tv_nsec = (timeout_ms % 1000) * 1000000;  // Convert milliseconds to
//     nanoseconds
//
//     IORING_OP_LINK_TIMEOUT
//     io_uring_prep_link_timeout(sqe_timeout, &ts, 0, 0);
//
//     /* Mark the read request as linked to the timeout */
//     sqe_read->flags |= IOSQE_IO_LINK;
//
//     /* Submit both SQEs as a linked operation */
//     if (io_uring_submit(ring) < 0)
//         fatal_error("io_uring_submit");
//
//     return 0;
// }

int handle_request(struct io_uring *ring, struct op *rop) {
  (void)ring;
  uint64_t client_id = extract_client_id(rop->pool_id);
  uint16_t store_idx = extract_pool_idx(rop->pool_id);

  for (size_t bytes = 0; bytes < rop->buf_len; bytes += sizeof(int)) {
    int msg_id = read_int_from_buffer(rop->buf + bytes);
    printf("[client id=%lu idx=%d fd=%d] received msg=%d\n", client_id,
           store_idx, rop->client_fd, msg_id);
  }

  return 0;
}

int start_server(struct io_uring *ring, int server_fd) {
  struct op *op_data;
  size_t bytes_read, bytes_written;

  if (add_accept(ring, server_fd) < 0)
    return -1;

  if (io_uring_submit(ring) < 0)
    fatal_error("io_uring_submit");

  struct io_uring_cqe *cqe;
  while (1) {
    if (io_uring_wait_cqe(ring, &cqe) < 0)
      fatal_error("io_uring_wait_cqe");

    /* Upon adding an accept request to the ring, we ensured allocation of
     * struct op so it is safe to index into client_ops and deref the ptr we get
     * back.
     */
    uint64_t pool_id = (uint64_t)io_uring_cqe_get_data(cqe);
    op_data = pool_get(pool_id);
    assert(op_data != NULL);

    // if (op_data->id != pool_id) {
    //   printf("op used by another pool_id=%lu old pool_id=%lu\n",
    //   op_data->pool_id,
    //          pool_id);
    //   io_uring_cqe_seen(ring, cqe);
    //   continue;
    // }

    // TODO: implement simple chat protocol.

    if (cqe->res < 0) {
      fprintf(stderr, "[fd=%d id=%lu] op %s failed: %s\n", op_data->client_fd,
              op_data->pool_id, op_type_str(op_data->type),
              strerror(-cqe->res));
      io_uring_cqe_seen(ring, cqe);
      pool_put(op_data, pool_id);
      continue;
    }

    switch (op_data->type) {
    case OP_ACCEPT:
      // printf("ACCEPT finished\n");
      if (add_accept(ring, server_fd) < 0)
        return -1;

      int client_fd = cqe->res;
      if (add_read(ring, op_data, client_fd) < 0)
        return -1;

      op_log_with_client_ip(op_data, "connected");

      if (io_uring_submit(ring) < 0)
        fatal_error("io_uring_submit");

      break;
    case OP_READ:
      // printf("READ finished\n");
      /* cqe->res isn't negative so it's safe to assign to size_t. */
      bytes_read = cqe->res;
      if (bytes_read == 0) {
        op_log_with_client_ip(op_data, "disconnected");
        pool_put(op_data, pool_id);
        break;
      }

      op_data->buf_len = bytes_read;
      // if (add_read(ring, op_data, op_data->client_fd) < 0)
      //   return -1;
      // handle_request(ring, op_data);
      if (add_write(ring, op_data, bytes_read) < 0) {
        return -1;
      }

      if (io_uring_submit(ring) < 0) {
        fatal_error("io_uring_submit");
      }
      break;
    case OP_WRITE:
      // printf("WRITE finished\n");
      /* cqe->res isn't negative so it's safe to assign to size_t. */
      bytes_written = cqe->res;
      if (bytes_written == 0) {
        printf("[fd=%d id=%lu] SHORT_WRITE_0\n", op_data->client_fd,
               op_data->pool_id);
      }

      if (op_is_incomplete(op_data, bytes_written)) {
        printf("SHORT WRITE fd=%d (%zu)/(%zu)\n", op_data->client_fd,
               bytes_written, op_data->buf_len - op_data->processed);

        if (add_short_write(ring, op_data, bytes_written) < 0) {
          return -1;
        }
      } else if (add_read(ring, op_data, op_data->client_fd) < 0) {
        return -1;
      }

      if (io_uring_submit(ring) < 0) {
        fatal_error("io_uring_submit");
      }

      break;
    default:
      printf("invalid operation type: %s\n", op_type_str(op_data->type));
      return -1;
    }

    io_uring_cqe_seen(ring, cqe);
  }
}

int main() {
  struct io_uring ring;
  int ret = io_uring_queue_init(QUEUE_SIZE, &ring, 0);
  if (ret < 0) {
    perror("io_uring_queue_init");
    return 1;
  }

  int server_fd = setup_server(PORT);

  printf("Server is running\n");

  if (start_server(&ring, server_fd) < 0) {
    fprintf(stderr, "failed to start server\n");
    return EXIT_FAILURE;
  }

  if (shutdown(server_fd, SHUT_RDWR) == -1)
    fatal_error("server shutdown");

  if (close(server_fd) == -1)
    fatal_error("server_fd close");
}
