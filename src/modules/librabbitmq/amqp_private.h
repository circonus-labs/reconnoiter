/* MOZILLA PUBLIC LICENSE Version 1.1 -- see LICENSE-MPL-RabbitMQ */
#ifndef librabbitmq_amqp_private_h
#define librabbitmq_amqp_private_h

#ifdef __cplusplus
extern "C" {
#endif

#include <arpa/inet.h> /* ntohl, htonl, ntohs, htons */
#include <unistd.h> /* write */

/*
 * Connection states:
 *
 * - CONNECTION_STATE_IDLE: initial state, and entered again after
 *   each frame is completed. Means that no bytes of the next frame
 *   have been seen yet. Connections may only be reconfigured, and the
 *   connection's pools recycled, when in this state. Whenever we're
 *   in this state, the inbound_buffer's bytes pointer must be NULL;
 *   any other state, and it must point to a block of memory allocated
 *   from the frame_pool.
 *
 * - CONNECTION_STATE_WAITING_FOR_HEADER: Some bytes of an incoming
 *   frame have been seen, but not a complete frame header's worth.
 *
 * - CONNECTION_STATE_WAITING_FOR_BODY: A complete frame header has
 *   been seen, but the frame is not yet complete. When it is
 *   completed, it will be returned, and the connection will return to
 *   IDLE state.
 *
 * - CONNECTION_STATE_WAITING_FOR_PROTOCOL_HEADER: The beginning of a
 *   protocol version header has been seen, but the full eight bytes
 *   hasn't yet been received. When it is completed, it will be
 *   returned, and the connection will return to IDLE state.
 *
 */
typedef enum amqp_connection_state_enum_ {
  CONNECTION_STATE_IDLE = 0,
  CONNECTION_STATE_WAITING_FOR_HEADER,
  CONNECTION_STATE_WAITING_FOR_BODY,
  CONNECTION_STATE_WAITING_FOR_PROTOCOL_HEADER
} amqp_connection_state_enum;

/* 7 bytes up front, then payload, then 1 byte footer */
#define HEADER_SIZE 7
#define FOOTER_SIZE 1

typedef struct amqp_link_t_ {
  struct amqp_link_t_ *next;
  void *data;
} amqp_link_t;

struct amqp_connection_state_t_ {
  amqp_pool_t frame_pool;
  amqp_pool_t decoding_pool;

  amqp_connection_state_enum state;

  int channel_max;
  int frame_max;
  int heartbeat;
  amqp_bytes_t inbound_buffer;

  size_t inbound_offset;
  size_t target_size;

  amqp_bytes_t outbound_buffer;

  int sockfd;
  amqp_bytes_t sock_inbound_buffer;
  size_t sock_inbound_offset;
  size_t sock_inbound_limit;

  amqp_link_t *first_queued_frame;
  amqp_link_t *last_queued_frame;

  amqp_basic_return_fn_t basic_return_callback;
  void *basic_return_callback_data;
};

static inline int eintr_safe_write(int fd, const void *buf, size_t count) {
  int ret, done=0, total=0;
  while (1) {
    ret = write(fd, buf+total, count-total);
    /* If we got an EINTR, ignore it and keep
       going */
    if ((ret == -1) && (errno == EINTR)) {
      continue;
    }
    /* If we got an error that wasn't EINTR,
       we failed. Break out */
    if (ret == -1) {
      done = 0;
      break;
    }
    total += ret;
    /* If we hit this, we're done. Flag that we're
       done and break out */
    if (total == count) {
      done = 1;
      break;
    }
    /* This is a weird case that should never happen...
       we should never write more than the initial amount.
       If we do somehow, it's an error */
    if (total > count) {
      done = 0;
      break;
    }
  }
  if (!done) total = -1;
  return total;
}

static inline int eintr_safe_read(int fd, void *buf, size_t count) {
  int ret=-1;
  while ((ret = read(fd, buf, count)) == -1 && (errno == EINTR));
  return ret;
}

#define CHECK_LIMIT(b, o, l, v) ({ if ((o + l) > (b).len) { return -EFAULT; } (v); })
#define BUF_AT(b, o) (&(((uint8_t *) (b).bytes)[o]))

#define D_8(b, o) CHECK_LIMIT(b, o, 1, * (uint8_t *) BUF_AT(b, o))
#define D_16(b, o) CHECK_LIMIT(b, o, 2, ({uint16_t v; memcpy(&v, BUF_AT(b, o), 2); ntohs(v);}))
#define D_32(b, o) CHECK_LIMIT(b, o, 4, ({uint32_t v; memcpy(&v, BUF_AT(b, o), 4); ntohl(v);}))
#define D_64(b, o) ({				\
  uint64_t hi = D_32(b, o);			\
  uint64_t lo = D_32(b, o + 4);			\
  hi << 32 | lo;				\
})

#define D_BYTES(b, o, l) CHECK_LIMIT(b, o, l, BUF_AT(b, o))

#define E_8(b, o, v) CHECK_LIMIT(b, o, 1, * (uint8_t *) BUF_AT(b, o) = (v))
#define E_16(b, o, v) CHECK_LIMIT(b, o, 2, ({uint16_t vv = htons(v); memcpy(BUF_AT(b, o), &vv, 2);}))
#define E_32(b, o, v) CHECK_LIMIT(b, o, 4, ({uint32_t vv = htonl(v); memcpy(BUF_AT(b, o), &vv, 4);}))
#define E_64(b, o, v) ({					\
      E_32(b, o, (uint32_t) (((uint64_t) v) >> 32));		\
      E_32(b, o + 4, (uint32_t) (((uint64_t) v) & 0xFFFFFFFF));	\
    })

#define E_BYTES(b, o, l, v) CHECK_LIMIT(b, o, l, memcpy(BUF_AT(b, o), (v), (l)))

extern int amqp_decode_table(amqp_bytes_t encoded,
			     amqp_pool_t *pool,
			     amqp_table_t *output,
			     int *offsetptr);

extern int amqp_encode_table(amqp_bytes_t encoded,
			     amqp_table_t *input,
			     int *offsetptr);

#define amqp_assert(condition, ...)		\
  ({						\
    if (!(condition)) {				\
      fprintf(stderr, __VA_ARGS__);		\
      fputc('\n', stderr);			\
      abort();					\
    }						\
  })

#define AMQP_CHECK_RESULT(expr)			\
  ({						\
    int _result = (expr);			\
    if (_result < 0) return _result;		\
    _result;					\
  })

#define AMQP_CHECK_EOF_RESULT(expr)		\
  ({						\
    int _result = (expr);			\
    if (_result <= 0) return _result;		\
    _result;					\
  })

#ifndef NDEBUG
extern void amqp_dump(void const *buffer, size_t len);
#else
#define amqp_dump(buffer, len) ((void) 0)
#endif

#ifdef __cplusplus
}
#endif

#endif
