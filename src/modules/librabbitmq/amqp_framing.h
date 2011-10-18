#ifndef librabbitmq_amqp_framing_h
#define librabbitmq_amqp_framing_h

#ifdef __cplusplus
extern "C" {
#endif

#define AMQP_PROTOCOL_VERSION_MAJOR 8
#define AMQP_PROTOCOL_VERSION_MINOR 0
#define AMQP_PROTOCOL_PORT 5672
#define AMQP_FRAME_METHOD 1
#define AMQP_FRAME_HEADER 2
#define AMQP_FRAME_BODY 3
#define AMQP_FRAME_OOB_METHOD 4
#define AMQP_FRAME_OOB_HEADER 5
#define AMQP_FRAME_OOB_BODY 6
#define AMQP_FRAME_TRACE 7
#define AMQP_FRAME_HEARTBEAT 8
#define AMQP_FRAME_MIN_SIZE 4096
#define AMQP_FRAME_END 206
#define AMQP_REPLY_SUCCESS 200
#define AMQP_NOT_DELIVERED 310
#define AMQP_CONTENT_TOO_LARGE 311
#define AMQP_NO_ROUTE 312
#define AMQP_NO_CONSUMERS 313
#define AMQP_ACCESS_REFUSED 403
#define AMQP_NOT_FOUND 404
#define AMQP_RESOURCE_LOCKED 405
#define AMQP_PRECONDITION_FAILED 406
#define AMQP_CONNECTION_FORCED 320
#define AMQP_INVALID_PATH 402
#define AMQP_FRAME_ERROR 501
#define AMQP_SYNTAX_ERROR 502
#define AMQP_COMMAND_INVALID 503
#define AMQP_CHANNEL_ERROR 504
#define AMQP_RESOURCE_ERROR 506
#define AMQP_NOT_ALLOWED 530
#define AMQP_NOT_IMPLEMENTED 540
#define AMQP_INTERNAL_ERROR 541

/* Function prototypes. */
extern char const *amqp_method_name(amqp_method_number_t methodNumber);
extern amqp_boolean_t amqp_method_has_content(amqp_method_number_t methodNumber);
extern int amqp_decode_method(amqp_method_number_t methodNumber,
                              amqp_pool_t *pool,
                              amqp_bytes_t encoded,
                              void **decoded);
extern int amqp_decode_properties(uint16_t class_id,
                                  amqp_pool_t *pool,
                                  amqp_bytes_t encoded,
                                  void **decoded);
extern int amqp_encode_method(amqp_method_number_t methodNumber,
                              void *decoded,
                              amqp_bytes_t encoded);
extern int amqp_encode_properties(uint16_t class_id,
                                  void *decoded,
                                  amqp_bytes_t encoded);

/* Method field records. */
#define AMQP_CONNECTION_START_METHOD ((amqp_method_number_t) 0x000A000A) /* 10, 10; 655370 */
typedef struct amqp_connection_start_t_ {
  uint8_t version_major;
  uint8_t version_minor;
  amqp_table_t server_properties;
  amqp_bytes_t mechanisms;
  amqp_bytes_t locales;
} amqp_connection_start_t;

#define AMQP_CONNECTION_START_OK_METHOD ((amqp_method_number_t) 0x000A000B) /* 10, 11; 655371 */
typedef struct amqp_connection_start_ok_t_ {
  amqp_table_t client_properties;
  amqp_bytes_t mechanism;
  amqp_bytes_t response;
  amqp_bytes_t locale;
} amqp_connection_start_ok_t;

#define AMQP_CONNECTION_SECURE_METHOD ((amqp_method_number_t) 0x000A0014) /* 10, 20; 655380 */
typedef struct amqp_connection_secure_t_ {
  amqp_bytes_t challenge;
} amqp_connection_secure_t;

#define AMQP_CONNECTION_SECURE_OK_METHOD ((amqp_method_number_t) 0x000A0015) /* 10, 21; 655381 */
typedef struct amqp_connection_secure_ok_t_ {
  amqp_bytes_t response;
} amqp_connection_secure_ok_t;

#define AMQP_CONNECTION_TUNE_METHOD ((amqp_method_number_t) 0x000A001E) /* 10, 30; 655390 */
typedef struct amqp_connection_tune_t_ {
  uint16_t channel_max;
  uint32_t frame_max;
  uint16_t heartbeat;
} amqp_connection_tune_t;

#define AMQP_CONNECTION_TUNE_OK_METHOD ((amqp_method_number_t) 0x000A001F) /* 10, 31; 655391 */
typedef struct amqp_connection_tune_ok_t_ {
  uint16_t channel_max;
  uint32_t frame_max;
  uint16_t heartbeat;
} amqp_connection_tune_ok_t;

#define AMQP_CONNECTION_OPEN_METHOD ((amqp_method_number_t) 0x000A0028) /* 10, 40; 655400 */
typedef struct amqp_connection_open_t_ {
  amqp_bytes_t virtual_host;
  amqp_bytes_t capabilities;
  amqp_boolean_t insist;
} amqp_connection_open_t;

#define AMQP_CONNECTION_OPEN_OK_METHOD ((amqp_method_number_t) 0x000A0029) /* 10, 41; 655401 */
typedef struct amqp_connection_open_ok_t_ {
  amqp_bytes_t known_hosts;
} amqp_connection_open_ok_t;

#define AMQP_CONNECTION_REDIRECT_METHOD ((amqp_method_number_t) 0x000A0032) /* 10, 50; 655410 */
typedef struct amqp_connection_redirect_t_ {
  amqp_bytes_t host;
  amqp_bytes_t known_hosts;
} amqp_connection_redirect_t;

#define AMQP_CONNECTION_CLOSE_METHOD ((amqp_method_number_t) 0x000A003C) /* 10, 60; 655420 */
typedef struct amqp_connection_close_t_ {
  uint16_t reply_code;
  amqp_bytes_t reply_text;
  uint16_t class_id;
  uint16_t method_id;
} amqp_connection_close_t;

#define AMQP_CONNECTION_CLOSE_OK_METHOD ((amqp_method_number_t) 0x000A003D) /* 10, 61; 655421 */
typedef struct amqp_connection_close_ok_t_ {
  unsigned unused:1;
} amqp_connection_close_ok_t;

#define AMQP_CHANNEL_OPEN_METHOD ((amqp_method_number_t) 0x0014000A) /* 20, 10; 1310730 */
typedef struct amqp_channel_open_t_ {
  amqp_bytes_t out_of_band;
} amqp_channel_open_t;

#define AMQP_CHANNEL_OPEN_OK_METHOD ((amqp_method_number_t) 0x0014000B) /* 20, 11; 1310731 */
typedef struct amqp_channel_open_ok_t_ {
  unsigned unused:1;
} amqp_channel_open_ok_t;

#define AMQP_CHANNEL_FLOW_METHOD ((amqp_method_number_t) 0x00140014) /* 20, 20; 1310740 */
typedef struct amqp_channel_flow_t_ {
  amqp_boolean_t active;
} amqp_channel_flow_t;

#define AMQP_CHANNEL_FLOW_OK_METHOD ((amqp_method_number_t) 0x00140015) /* 20, 21; 1310741 */
typedef struct amqp_channel_flow_ok_t_ {
  amqp_boolean_t active;
} amqp_channel_flow_ok_t;

#define AMQP_CHANNEL_ALERT_METHOD ((amqp_method_number_t) 0x0014001E) /* 20, 30; 1310750 */
typedef struct amqp_channel_alert_t_ {
  uint16_t reply_code;
  amqp_bytes_t reply_text;
  amqp_table_t details;
} amqp_channel_alert_t;

#define AMQP_CHANNEL_CLOSE_METHOD ((amqp_method_number_t) 0x00140028) /* 20, 40; 1310760 */
typedef struct amqp_channel_close_t_ {
  uint16_t reply_code;
  amqp_bytes_t reply_text;
  uint16_t class_id;
  uint16_t method_id;
} amqp_channel_close_t;

#define AMQP_CHANNEL_CLOSE_OK_METHOD ((amqp_method_number_t) 0x00140029) /* 20, 41; 1310761 */
typedef struct amqp_channel_close_ok_t_ {
  unsigned unused:1;
} amqp_channel_close_ok_t;

#define AMQP_ACCESS_REQUEST_METHOD ((amqp_method_number_t) 0x001E000A) /* 30, 10; 1966090 */
typedef struct amqp_access_request_t_ {
  amqp_bytes_t realm;
  amqp_boolean_t exclusive;
  amqp_boolean_t passive;
  amqp_boolean_t active;
  amqp_boolean_t write;
  amqp_boolean_t read;
} amqp_access_request_t;

#define AMQP_ACCESS_REQUEST_OK_METHOD ((amqp_method_number_t) 0x001E000B) /* 30, 11; 1966091 */
typedef struct amqp_access_request_ok_t_ {
  uint16_t ticket;
} amqp_access_request_ok_t;

#define AMQP_EXCHANGE_DECLARE_METHOD ((amqp_method_number_t) 0x0028000A) /* 40, 10; 2621450 */
typedef struct amqp_exchange_declare_t_ {
  uint16_t ticket;
  amqp_bytes_t exchange;
  amqp_bytes_t type;
  amqp_boolean_t passive;
  amqp_boolean_t durable;
  amqp_boolean_t auto_delete;
  amqp_boolean_t internal;
  amqp_boolean_t nowait;
  amqp_table_t arguments;
} amqp_exchange_declare_t;

#define AMQP_EXCHANGE_DECLARE_OK_METHOD ((amqp_method_number_t) 0x0028000B) /* 40, 11; 2621451 */
typedef struct amqp_exchange_declare_ok_t_ {
  unsigned unused:1;
} amqp_exchange_declare_ok_t;

#define AMQP_EXCHANGE_DELETE_METHOD ((amqp_method_number_t) 0x00280014) /* 40, 20; 2621460 */
typedef struct amqp_exchange_delete_t_ {
  uint16_t ticket;
  amqp_bytes_t exchange;
  amqp_boolean_t if_unused;
  amqp_boolean_t nowait;
} amqp_exchange_delete_t;

#define AMQP_EXCHANGE_DELETE_OK_METHOD ((amqp_method_number_t) 0x00280015) /* 40, 21; 2621461 */
typedef struct amqp_exchange_delete_ok_t_ {
  unsigned unused:1;
} amqp_exchange_delete_ok_t;

#define AMQP_QUEUE_DECLARE_METHOD ((amqp_method_number_t) 0x0032000A) /* 50, 10; 3276810 */
typedef struct amqp_queue_declare_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_boolean_t passive;
  amqp_boolean_t durable;
  amqp_boolean_t exclusive;
  amqp_boolean_t auto_delete;
  amqp_boolean_t nowait;
  amqp_table_t arguments;
} amqp_queue_declare_t;

#define AMQP_QUEUE_DECLARE_OK_METHOD ((amqp_method_number_t) 0x0032000B) /* 50, 11; 3276811 */
typedef struct amqp_queue_declare_ok_t_ {
  amqp_bytes_t queue;
  uint32_t message_count;
  uint32_t consumer_count;
} amqp_queue_declare_ok_t;

#define AMQP_QUEUE_BIND_METHOD ((amqp_method_number_t) 0x00320014) /* 50, 20; 3276820 */
typedef struct amqp_queue_bind_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  amqp_boolean_t nowait;
  amqp_table_t arguments;
} amqp_queue_bind_t;

#define AMQP_QUEUE_BIND_OK_METHOD ((amqp_method_number_t) 0x00320015) /* 50, 21; 3276821 */
typedef struct amqp_queue_bind_ok_t_ {
  unsigned unused:1;
} amqp_queue_bind_ok_t;

#define AMQP_QUEUE_PURGE_METHOD ((amqp_method_number_t) 0x0032001E) /* 50, 30; 3276830 */
typedef struct amqp_queue_purge_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_boolean_t nowait;
} amqp_queue_purge_t;

#define AMQP_QUEUE_PURGE_OK_METHOD ((amqp_method_number_t) 0x0032001F) /* 50, 31; 3276831 */
typedef struct amqp_queue_purge_ok_t_ {
  uint32_t message_count;
} amqp_queue_purge_ok_t;

#define AMQP_QUEUE_DELETE_METHOD ((amqp_method_number_t) 0x00320028) /* 50, 40; 3276840 */
typedef struct amqp_queue_delete_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_boolean_t if_unused;
  amqp_boolean_t if_empty;
  amqp_boolean_t nowait;
} amqp_queue_delete_t;

#define AMQP_QUEUE_DELETE_OK_METHOD ((amqp_method_number_t) 0x00320029) /* 50, 41; 3276841 */
typedef struct amqp_queue_delete_ok_t_ {
  uint32_t message_count;
} amqp_queue_delete_ok_t;

#define AMQP_QUEUE_UNBIND_METHOD ((amqp_method_number_t) 0x00320032) /* 50, 50; 3276850 */
typedef struct amqp_queue_unbind_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  amqp_table_t arguments;
} amqp_queue_unbind_t;

#define AMQP_QUEUE_UNBIND_OK_METHOD ((amqp_method_number_t) 0x00320033) /* 50, 51; 3276851 */
typedef struct amqp_queue_unbind_ok_t_ {
  unsigned unused:1;
} amqp_queue_unbind_ok_t;

#define AMQP_BASIC_QOS_METHOD ((amqp_method_number_t) 0x003C000A) /* 60, 10; 3932170 */
typedef struct amqp_basic_qos_t_ {
  uint32_t prefetch_size;
  uint16_t prefetch_count;
  amqp_boolean_t global;
} amqp_basic_qos_t;

#define AMQP_BASIC_QOS_OK_METHOD ((amqp_method_number_t) 0x003C000B) /* 60, 11; 3932171 */
typedef struct amqp_basic_qos_ok_t_ {
  unsigned unused:1;
} amqp_basic_qos_ok_t;

#define AMQP_BASIC_CONSUME_METHOD ((amqp_method_number_t) 0x003C0014) /* 60, 20; 3932180 */
typedef struct amqp_basic_consume_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_bytes_t consumer_tag;
  amqp_boolean_t no_local;
  amqp_boolean_t no_ack;
  amqp_boolean_t exclusive;
  amqp_boolean_t nowait;
} amqp_basic_consume_t;

#define AMQP_BASIC_CONSUME_OK_METHOD ((amqp_method_number_t) 0x003C0015) /* 60, 21; 3932181 */
typedef struct amqp_basic_consume_ok_t_ {
  amqp_bytes_t consumer_tag;
} amqp_basic_consume_ok_t;

#define AMQP_BASIC_CANCEL_METHOD ((amqp_method_number_t) 0x003C001E) /* 60, 30; 3932190 */
typedef struct amqp_basic_cancel_t_ {
  amqp_bytes_t consumer_tag;
  amqp_boolean_t nowait;
} amqp_basic_cancel_t;

#define AMQP_BASIC_CANCEL_OK_METHOD ((amqp_method_number_t) 0x003C001F) /* 60, 31; 3932191 */
typedef struct amqp_basic_cancel_ok_t_ {
  amqp_bytes_t consumer_tag;
} amqp_basic_cancel_ok_t;

#define AMQP_BASIC_PUBLISH_METHOD ((amqp_method_number_t) 0x003C0028) /* 60, 40; 3932200 */
typedef struct amqp_basic_publish_t_ {
  uint16_t ticket;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  amqp_boolean_t mandatory;
  amqp_boolean_t immediate;
} amqp_basic_publish_t;

#define AMQP_BASIC_RETURN_METHOD ((amqp_method_number_t) 0x003C0032) /* 60, 50; 3932210 */
/* typedef in amqp.h for callback */
struct amqp_basic_return_t_ {
  uint16_t reply_code;
  amqp_bytes_t reply_text;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
};

#define AMQP_BASIC_DELIVER_METHOD ((amqp_method_number_t) 0x003C003C) /* 60, 60; 3932220 */
typedef struct amqp_basic_deliver_t_ {
  amqp_bytes_t consumer_tag;
  uint64_t delivery_tag;
  amqp_boolean_t redelivered;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
} amqp_basic_deliver_t;

#define AMQP_BASIC_GET_METHOD ((amqp_method_number_t) 0x003C0046) /* 60, 70; 3932230 */
typedef struct amqp_basic_get_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_boolean_t no_ack;
} amqp_basic_get_t;

#define AMQP_BASIC_GET_OK_METHOD ((amqp_method_number_t) 0x003C0047) /* 60, 71; 3932231 */
typedef struct amqp_basic_get_ok_t_ {
  uint64_t delivery_tag;
  amqp_boolean_t redelivered;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  uint32_t message_count;
} amqp_basic_get_ok_t;

#define AMQP_BASIC_GET_EMPTY_METHOD ((amqp_method_number_t) 0x003C0048) /* 60, 72; 3932232 */
typedef struct amqp_basic_get_empty_t_ {
  amqp_bytes_t cluster_id;
} amqp_basic_get_empty_t;

#define AMQP_BASIC_ACK_METHOD ((amqp_method_number_t) 0x003C0050) /* 60, 80; 3932240 */
typedef struct amqp_basic_ack_t_ {
  uint64_t delivery_tag;
  amqp_boolean_t multiple;
} amqp_basic_ack_t;

#define AMQP_BASIC_REJECT_METHOD ((amqp_method_number_t) 0x003C005A) /* 60, 90; 3932250 */
typedef struct amqp_basic_reject_t_ {
  uint64_t delivery_tag;
  amqp_boolean_t requeue;
} amqp_basic_reject_t;

#define AMQP_BASIC_RECOVER_METHOD ((amqp_method_number_t) 0x003C0064) /* 60, 100; 3932260 */
typedef struct amqp_basic_recover_t_ {
  amqp_boolean_t requeue;
} amqp_basic_recover_t;

#define AMQP_FILE_QOS_METHOD ((amqp_method_number_t) 0x0046000A) /* 70, 10; 4587530 */
typedef struct amqp_file_qos_t_ {
  uint32_t prefetch_size;
  uint16_t prefetch_count;
  amqp_boolean_t global;
} amqp_file_qos_t;

#define AMQP_FILE_QOS_OK_METHOD ((amqp_method_number_t) 0x0046000B) /* 70, 11; 4587531 */
typedef struct amqp_file_qos_ok_t_ {
  unsigned unused:1;
} amqp_file_qos_ok_t;

#define AMQP_FILE_CONSUME_METHOD ((amqp_method_number_t) 0x00460014) /* 70, 20; 4587540 */
typedef struct amqp_file_consume_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_bytes_t consumer_tag;
  amqp_boolean_t no_local;
  amqp_boolean_t no_ack;
  amqp_boolean_t exclusive;
  amqp_boolean_t nowait;
} amqp_file_consume_t;

#define AMQP_FILE_CONSUME_OK_METHOD ((amqp_method_number_t) 0x00460015) /* 70, 21; 4587541 */
typedef struct amqp_file_consume_ok_t_ {
  amqp_bytes_t consumer_tag;
} amqp_file_consume_ok_t;

#define AMQP_FILE_CANCEL_METHOD ((amqp_method_number_t) 0x0046001E) /* 70, 30; 4587550 */
typedef struct amqp_file_cancel_t_ {
  amqp_bytes_t consumer_tag;
  amqp_boolean_t nowait;
} amqp_file_cancel_t;

#define AMQP_FILE_CANCEL_OK_METHOD ((amqp_method_number_t) 0x0046001F) /* 70, 31; 4587551 */
typedef struct amqp_file_cancel_ok_t_ {
  amqp_bytes_t consumer_tag;
} amqp_file_cancel_ok_t;

#define AMQP_FILE_OPEN_METHOD ((amqp_method_number_t) 0x00460028) /* 70, 40; 4587560 */
typedef struct amqp_file_open_t_ {
  amqp_bytes_t identifier;
  uint64_t content_size;
} amqp_file_open_t;

#define AMQP_FILE_OPEN_OK_METHOD ((amqp_method_number_t) 0x00460029) /* 70, 41; 4587561 */
typedef struct amqp_file_open_ok_t_ {
  uint64_t staged_size;
} amqp_file_open_ok_t;

#define AMQP_FILE_STAGE_METHOD ((amqp_method_number_t) 0x00460032) /* 70, 50; 4587570 */
typedef struct amqp_file_stage_t_ {
  unsigned unused:1;
} amqp_file_stage_t;

#define AMQP_FILE_PUBLISH_METHOD ((amqp_method_number_t) 0x0046003C) /* 70, 60; 4587580 */
typedef struct amqp_file_publish_t_ {
  uint16_t ticket;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  amqp_boolean_t mandatory;
  amqp_boolean_t immediate;
  amqp_bytes_t identifier;
} amqp_file_publish_t;

#define AMQP_FILE_RETURN_METHOD ((amqp_method_number_t) 0x00460046) /* 70, 70; 4587590 */
typedef struct amqp_file_return_t_ {
  uint16_t reply_code;
  amqp_bytes_t reply_text;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
} amqp_file_return_t;

#define AMQP_FILE_DELIVER_METHOD ((amqp_method_number_t) 0x00460050) /* 70, 80; 4587600 */
typedef struct amqp_file_deliver_t_ {
  amqp_bytes_t consumer_tag;
  uint64_t delivery_tag;
  amqp_boolean_t redelivered;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  amqp_bytes_t identifier;
} amqp_file_deliver_t;

#define AMQP_FILE_ACK_METHOD ((amqp_method_number_t) 0x0046005A) /* 70, 90; 4587610 */
typedef struct amqp_file_ack_t_ {
  uint64_t delivery_tag;
  amqp_boolean_t multiple;
} amqp_file_ack_t;

#define AMQP_FILE_REJECT_METHOD ((amqp_method_number_t) 0x00460064) /* 70, 100; 4587620 */
typedef struct amqp_file_reject_t_ {
  uint64_t delivery_tag;
  amqp_boolean_t requeue;
} amqp_file_reject_t;

#define AMQP_STREAM_QOS_METHOD ((amqp_method_number_t) 0x0050000A) /* 80, 10; 5242890 */
typedef struct amqp_stream_qos_t_ {
  uint32_t prefetch_size;
  uint16_t prefetch_count;
  uint32_t consume_rate;
  amqp_boolean_t global;
} amqp_stream_qos_t;

#define AMQP_STREAM_QOS_OK_METHOD ((amqp_method_number_t) 0x0050000B) /* 80, 11; 5242891 */
typedef struct amqp_stream_qos_ok_t_ {
  unsigned unused:1;
} amqp_stream_qos_ok_t;

#define AMQP_STREAM_CONSUME_METHOD ((amqp_method_number_t) 0x00500014) /* 80, 20; 5242900 */
typedef struct amqp_stream_consume_t_ {
  uint16_t ticket;
  amqp_bytes_t queue;
  amqp_bytes_t consumer_tag;
  amqp_boolean_t no_local;
  amqp_boolean_t exclusive;
  amqp_boolean_t nowait;
} amqp_stream_consume_t;

#define AMQP_STREAM_CONSUME_OK_METHOD ((amqp_method_number_t) 0x00500015) /* 80, 21; 5242901 */
typedef struct amqp_stream_consume_ok_t_ {
  amqp_bytes_t consumer_tag;
} amqp_stream_consume_ok_t;

#define AMQP_STREAM_CANCEL_METHOD ((amqp_method_number_t) 0x0050001E) /* 80, 30; 5242910 */
typedef struct amqp_stream_cancel_t_ {
  amqp_bytes_t consumer_tag;
  amqp_boolean_t nowait;
} amqp_stream_cancel_t;

#define AMQP_STREAM_CANCEL_OK_METHOD ((amqp_method_number_t) 0x0050001F) /* 80, 31; 5242911 */
typedef struct amqp_stream_cancel_ok_t_ {
  amqp_bytes_t consumer_tag;
} amqp_stream_cancel_ok_t;

#define AMQP_STREAM_PUBLISH_METHOD ((amqp_method_number_t) 0x00500028) /* 80, 40; 5242920 */
typedef struct amqp_stream_publish_t_ {
  uint16_t ticket;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
  amqp_boolean_t mandatory;
  amqp_boolean_t immediate;
} amqp_stream_publish_t;

#define AMQP_STREAM_RETURN_METHOD ((amqp_method_number_t) 0x00500032) /* 80, 50; 5242930 */
typedef struct amqp_stream_return_t_ {
  uint16_t reply_code;
  amqp_bytes_t reply_text;
  amqp_bytes_t exchange;
  amqp_bytes_t routing_key;
} amqp_stream_return_t;

#define AMQP_STREAM_DELIVER_METHOD ((amqp_method_number_t) 0x0050003C) /* 80, 60; 5242940 */
typedef struct amqp_stream_deliver_t_ {
  amqp_bytes_t consumer_tag;
  uint64_t delivery_tag;
  amqp_bytes_t exchange;
  amqp_bytes_t queue;
} amqp_stream_deliver_t;

#define AMQP_TX_SELECT_METHOD ((amqp_method_number_t) 0x005A000A) /* 90, 10; 5898250 */
typedef struct amqp_tx_select_t_ {
  unsigned unused:1;
} amqp_tx_select_t;

#define AMQP_TX_SELECT_OK_METHOD ((amqp_method_number_t) 0x005A000B) /* 90, 11; 5898251 */
typedef struct amqp_tx_select_ok_t_ {
  unsigned unused:1;
} amqp_tx_select_ok_t;

#define AMQP_TX_COMMIT_METHOD ((amqp_method_number_t) 0x005A0014) /* 90, 20; 5898260 */
typedef struct amqp_tx_commit_t_ {
  unsigned unused:1;
} amqp_tx_commit_t;

#define AMQP_TX_COMMIT_OK_METHOD ((amqp_method_number_t) 0x005A0015) /* 90, 21; 5898261 */
typedef struct amqp_tx_commit_ok_t_ {
  unsigned unused:1;
} amqp_tx_commit_ok_t;

#define AMQP_TX_ROLLBACK_METHOD ((amqp_method_number_t) 0x005A001E) /* 90, 30; 5898270 */
typedef struct amqp_tx_rollback_t_ {
  unsigned unused:1;
} amqp_tx_rollback_t;

#define AMQP_TX_ROLLBACK_OK_METHOD ((amqp_method_number_t) 0x005A001F) /* 90, 31; 5898271 */
typedef struct amqp_tx_rollback_ok_t_ {
  unsigned unused:1;
} amqp_tx_rollback_ok_t;

#define AMQP_DTX_SELECT_METHOD ((amqp_method_number_t) 0x0064000A) /* 100, 10; 6553610 */
typedef struct amqp_dtx_select_t_ {
  unsigned unused:1;
} amqp_dtx_select_t;

#define AMQP_DTX_SELECT_OK_METHOD ((amqp_method_number_t) 0x0064000B) /* 100, 11; 6553611 */
typedef struct amqp_dtx_select_ok_t_ {
  unsigned unused:1;
} amqp_dtx_select_ok_t;

#define AMQP_DTX_START_METHOD ((amqp_method_number_t) 0x00640014) /* 100, 20; 6553620 */
typedef struct amqp_dtx_start_t_ {
  amqp_bytes_t dtx_identifier;
} amqp_dtx_start_t;

#define AMQP_DTX_START_OK_METHOD ((amqp_method_number_t) 0x00640015) /* 100, 21; 6553621 */
typedef struct amqp_dtx_start_ok_t_ {
  unsigned unused:1;
} amqp_dtx_start_ok_t;

#define AMQP_TUNNEL_REQUEST_METHOD ((amqp_method_number_t) 0x006E000A) /* 110, 10; 7208970 */
typedef struct amqp_tunnel_request_t_ {
  amqp_table_t meta_data;
} amqp_tunnel_request_t;

#define AMQP_TEST_INTEGER_METHOD ((amqp_method_number_t) 0x0078000A) /* 120, 10; 7864330 */
typedef struct amqp_test_integer_t_ {
  uint8_t integer_1;
  uint16_t integer_2;
  uint32_t integer_3;
  uint64_t integer_4;
  uint8_t operation;
} amqp_test_integer_t;

#define AMQP_TEST_INTEGER_OK_METHOD ((amqp_method_number_t) 0x0078000B) /* 120, 11; 7864331 */
typedef struct amqp_test_integer_ok_t_ {
  uint64_t result;
} amqp_test_integer_ok_t;

#define AMQP_TEST_STRING_METHOD ((amqp_method_number_t) 0x00780014) /* 120, 20; 7864340 */
typedef struct amqp_test_string_t_ {
  amqp_bytes_t string_1;
  amqp_bytes_t string_2;
  uint8_t operation;
} amqp_test_string_t;

#define AMQP_TEST_STRING_OK_METHOD ((amqp_method_number_t) 0x00780015) /* 120, 21; 7864341 */
typedef struct amqp_test_string_ok_t_ {
  amqp_bytes_t result;
} amqp_test_string_ok_t;

#define AMQP_TEST_TABLE_METHOD ((amqp_method_number_t) 0x0078001E) /* 120, 30; 7864350 */
typedef struct amqp_test_table_t_ {
  amqp_table_t table;
  uint8_t integer_op;
  uint8_t string_op;
} amqp_test_table_t;

#define AMQP_TEST_TABLE_OK_METHOD ((amqp_method_number_t) 0x0078001F) /* 120, 31; 7864351 */
typedef struct amqp_test_table_ok_t_ {
  uint64_t integer_result;
  amqp_bytes_t string_result;
} amqp_test_table_ok_t;

#define AMQP_TEST_CONTENT_METHOD ((amqp_method_number_t) 0x00780028) /* 120, 40; 7864360 */
typedef struct amqp_test_content_t_ {
  unsigned unused:1;
} amqp_test_content_t;

#define AMQP_TEST_CONTENT_OK_METHOD ((amqp_method_number_t) 0x00780029) /* 120, 41; 7864361 */
typedef struct amqp_test_content_ok_t_ {
  uint32_t content_checksum;
} amqp_test_content_ok_t;

/* Class property records. */
#define AMQP_CONNECTION_CLASS (0x000A) /* 10 */
typedef struct amqp_connection_properties_t_ {
  amqp_flags_t _flags;
} amqp_connection_properties_t;

#define AMQP_CHANNEL_CLASS (0x0014) /* 20 */
typedef struct amqp_channel_properties_t_ {
  amqp_flags_t _flags;
} amqp_channel_properties_t;

#define AMQP_ACCESS_CLASS (0x001E) /* 30 */
typedef struct amqp_access_properties_t_ {
  amqp_flags_t _flags;
} amqp_access_properties_t;

#define AMQP_EXCHANGE_CLASS (0x0028) /* 40 */
typedef struct amqp_exchange_properties_t_ {
  amqp_flags_t _flags;
} amqp_exchange_properties_t;

#define AMQP_QUEUE_CLASS (0x0032) /* 50 */
typedef struct amqp_queue_properties_t_ {
  amqp_flags_t _flags;
} amqp_queue_properties_t;

#define AMQP_BASIC_CLASS (0x003C) /* 60 */
#define AMQP_BASIC_CONTENT_TYPE_FLAG (1 << 15)
#define AMQP_BASIC_CONTENT_ENCODING_FLAG (1 << 14)
#define AMQP_BASIC_HEADERS_FLAG (1 << 13)
#define AMQP_BASIC_DELIVERY_MODE_FLAG (1 << 12)
#define AMQP_BASIC_PRIORITY_FLAG (1 << 11)
#define AMQP_BASIC_CORRELATION_ID_FLAG (1 << 10)
#define AMQP_BASIC_REPLY_TO_FLAG (1 << 9)
#define AMQP_BASIC_EXPIRATION_FLAG (1 << 8)
#define AMQP_BASIC_MESSAGE_ID_FLAG (1 << 7)
#define AMQP_BASIC_TIMESTAMP_FLAG (1 << 6)
#define AMQP_BASIC_TYPE_FLAG (1 << 5)
#define AMQP_BASIC_USER_ID_FLAG (1 << 4)
#define AMQP_BASIC_APP_ID_FLAG (1 << 3)
#define AMQP_BASIC_CLUSTER_ID_FLAG (1 << 2)
typedef struct amqp_basic_properties_t_ {
  amqp_flags_t _flags;
  amqp_bytes_t content_type;
  amqp_bytes_t content_encoding;
  amqp_table_t headers;
  uint8_t delivery_mode;
  uint8_t priority;
  amqp_bytes_t correlation_id;
  amqp_bytes_t reply_to;
  amqp_bytes_t expiration;
  amqp_bytes_t message_id;
  uint64_t timestamp;
  amqp_bytes_t type;
  amqp_bytes_t user_id;
  amqp_bytes_t app_id;
  amqp_bytes_t cluster_id;
} amqp_basic_properties_t;

#define AMQP_FILE_CLASS (0x0046) /* 70 */
#define AMQP_FILE_CONTENT_TYPE_FLAG (1 << 15)
#define AMQP_FILE_CONTENT_ENCODING_FLAG (1 << 14)
#define AMQP_FILE_HEADERS_FLAG (1 << 13)
#define AMQP_FILE_PRIORITY_FLAG (1 << 12)
#define AMQP_FILE_REPLY_TO_FLAG (1 << 11)
#define AMQP_FILE_MESSAGE_ID_FLAG (1 << 10)
#define AMQP_FILE_FILENAME_FLAG (1 << 9)
#define AMQP_FILE_TIMESTAMP_FLAG (1 << 8)
#define AMQP_FILE_CLUSTER_ID_FLAG (1 << 7)
typedef struct amqp_file_properties_t_ {
  amqp_flags_t _flags;
  amqp_bytes_t content_type;
  amqp_bytes_t content_encoding;
  amqp_table_t headers;
  uint8_t priority;
  amqp_bytes_t reply_to;
  amqp_bytes_t message_id;
  amqp_bytes_t filename;
  uint64_t timestamp;
  amqp_bytes_t cluster_id;
} amqp_file_properties_t;

#define AMQP_STREAM_CLASS (0x0050) /* 80 */
#define AMQP_STREAM_CONTENT_TYPE_FLAG (1 << 15)
#define AMQP_STREAM_CONTENT_ENCODING_FLAG (1 << 14)
#define AMQP_STREAM_HEADERS_FLAG (1 << 13)
#define AMQP_STREAM_PRIORITY_FLAG (1 << 12)
#define AMQP_STREAM_TIMESTAMP_FLAG (1 << 11)
typedef struct amqp_stream_properties_t_ {
  amqp_flags_t _flags;
  amqp_bytes_t content_type;
  amqp_bytes_t content_encoding;
  amqp_table_t headers;
  uint8_t priority;
  uint64_t timestamp;
} amqp_stream_properties_t;

#define AMQP_TX_CLASS (0x005A) /* 90 */
typedef struct amqp_tx_properties_t_ {
  amqp_flags_t _flags;
} amqp_tx_properties_t;

#define AMQP_DTX_CLASS (0x0064) /* 100 */
typedef struct amqp_dtx_properties_t_ {
  amqp_flags_t _flags;
} amqp_dtx_properties_t;

#define AMQP_TUNNEL_CLASS (0x006E) /* 110 */
#define AMQP_TUNNEL_HEADERS_FLAG (1 << 15)
#define AMQP_TUNNEL_PROXY_NAME_FLAG (1 << 14)
#define AMQP_TUNNEL_DATA_NAME_FLAG (1 << 13)
#define AMQP_TUNNEL_DURABLE_FLAG (1 << 12)
#define AMQP_TUNNEL_BROADCAST_FLAG (1 << 11)
typedef struct amqp_tunnel_properties_t_ {
  amqp_flags_t _flags;
  amqp_table_t headers;
  amqp_bytes_t proxy_name;
  amqp_bytes_t data_name;
  uint8_t durable;
  uint8_t broadcast;
} amqp_tunnel_properties_t;

#define AMQP_TEST_CLASS (0x0078) /* 120 */
typedef struct amqp_test_properties_t_ {
  amqp_flags_t _flags;
} amqp_test_properties_t;

#ifdef __cplusplus
}
#endif

#endif
