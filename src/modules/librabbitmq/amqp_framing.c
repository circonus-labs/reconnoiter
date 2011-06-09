#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h> /* ntohl, htonl, ntohs, htons */

#include "amqp.h"
#include "amqp_framing.h"
#include "amqp_private.h"

char const *amqp_method_name(amqp_method_number_t methodNumber) {
  switch (methodNumber) {
    case AMQP_CONNECTION_START_METHOD: return "AMQP_CONNECTION_START_METHOD";
    case AMQP_CONNECTION_START_OK_METHOD: return "AMQP_CONNECTION_START_OK_METHOD";
    case AMQP_CONNECTION_SECURE_METHOD: return "AMQP_CONNECTION_SECURE_METHOD";
    case AMQP_CONNECTION_SECURE_OK_METHOD: return "AMQP_CONNECTION_SECURE_OK_METHOD";
    case AMQP_CONNECTION_TUNE_METHOD: return "AMQP_CONNECTION_TUNE_METHOD";
    case AMQP_CONNECTION_TUNE_OK_METHOD: return "AMQP_CONNECTION_TUNE_OK_METHOD";
    case AMQP_CONNECTION_OPEN_METHOD: return "AMQP_CONNECTION_OPEN_METHOD";
    case AMQP_CONNECTION_OPEN_OK_METHOD: return "AMQP_CONNECTION_OPEN_OK_METHOD";
    case AMQP_CONNECTION_REDIRECT_METHOD: return "AMQP_CONNECTION_REDIRECT_METHOD";
    case AMQP_CONNECTION_CLOSE_METHOD: return "AMQP_CONNECTION_CLOSE_METHOD";
    case AMQP_CONNECTION_CLOSE_OK_METHOD: return "AMQP_CONNECTION_CLOSE_OK_METHOD";
    case AMQP_CHANNEL_OPEN_METHOD: return "AMQP_CHANNEL_OPEN_METHOD";
    case AMQP_CHANNEL_OPEN_OK_METHOD: return "AMQP_CHANNEL_OPEN_OK_METHOD";
    case AMQP_CHANNEL_FLOW_METHOD: return "AMQP_CHANNEL_FLOW_METHOD";
    case AMQP_CHANNEL_FLOW_OK_METHOD: return "AMQP_CHANNEL_FLOW_OK_METHOD";
    case AMQP_CHANNEL_ALERT_METHOD: return "AMQP_CHANNEL_ALERT_METHOD";
    case AMQP_CHANNEL_CLOSE_METHOD: return "AMQP_CHANNEL_CLOSE_METHOD";
    case AMQP_CHANNEL_CLOSE_OK_METHOD: return "AMQP_CHANNEL_CLOSE_OK_METHOD";
    case AMQP_ACCESS_REQUEST_METHOD: return "AMQP_ACCESS_REQUEST_METHOD";
    case AMQP_ACCESS_REQUEST_OK_METHOD: return "AMQP_ACCESS_REQUEST_OK_METHOD";
    case AMQP_EXCHANGE_DECLARE_METHOD: return "AMQP_EXCHANGE_DECLARE_METHOD";
    case AMQP_EXCHANGE_DECLARE_OK_METHOD: return "AMQP_EXCHANGE_DECLARE_OK_METHOD";
    case AMQP_EXCHANGE_DELETE_METHOD: return "AMQP_EXCHANGE_DELETE_METHOD";
    case AMQP_EXCHANGE_DELETE_OK_METHOD: return "AMQP_EXCHANGE_DELETE_OK_METHOD";
    case AMQP_QUEUE_DECLARE_METHOD: return "AMQP_QUEUE_DECLARE_METHOD";
    case AMQP_QUEUE_DECLARE_OK_METHOD: return "AMQP_QUEUE_DECLARE_OK_METHOD";
    case AMQP_QUEUE_BIND_METHOD: return "AMQP_QUEUE_BIND_METHOD";
    case AMQP_QUEUE_BIND_OK_METHOD: return "AMQP_QUEUE_BIND_OK_METHOD";
    case AMQP_QUEUE_PURGE_METHOD: return "AMQP_QUEUE_PURGE_METHOD";
    case AMQP_QUEUE_PURGE_OK_METHOD: return "AMQP_QUEUE_PURGE_OK_METHOD";
    case AMQP_QUEUE_DELETE_METHOD: return "AMQP_QUEUE_DELETE_METHOD";
    case AMQP_QUEUE_DELETE_OK_METHOD: return "AMQP_QUEUE_DELETE_OK_METHOD";
    case AMQP_QUEUE_UNBIND_METHOD: return "AMQP_QUEUE_UNBIND_METHOD";
    case AMQP_QUEUE_UNBIND_OK_METHOD: return "AMQP_QUEUE_UNBIND_OK_METHOD";
    case AMQP_BASIC_QOS_METHOD: return "AMQP_BASIC_QOS_METHOD";
    case AMQP_BASIC_QOS_OK_METHOD: return "AMQP_BASIC_QOS_OK_METHOD";
    case AMQP_BASIC_CONSUME_METHOD: return "AMQP_BASIC_CONSUME_METHOD";
    case AMQP_BASIC_CONSUME_OK_METHOD: return "AMQP_BASIC_CONSUME_OK_METHOD";
    case AMQP_BASIC_CANCEL_METHOD: return "AMQP_BASIC_CANCEL_METHOD";
    case AMQP_BASIC_CANCEL_OK_METHOD: return "AMQP_BASIC_CANCEL_OK_METHOD";
    case AMQP_BASIC_PUBLISH_METHOD: return "AMQP_BASIC_PUBLISH_METHOD";
    case AMQP_BASIC_RETURN_METHOD: return "AMQP_BASIC_RETURN_METHOD";
    case AMQP_BASIC_DELIVER_METHOD: return "AMQP_BASIC_DELIVER_METHOD";
    case AMQP_BASIC_GET_METHOD: return "AMQP_BASIC_GET_METHOD";
    case AMQP_BASIC_GET_OK_METHOD: return "AMQP_BASIC_GET_OK_METHOD";
    case AMQP_BASIC_GET_EMPTY_METHOD: return "AMQP_BASIC_GET_EMPTY_METHOD";
    case AMQP_BASIC_ACK_METHOD: return "AMQP_BASIC_ACK_METHOD";
    case AMQP_BASIC_REJECT_METHOD: return "AMQP_BASIC_REJECT_METHOD";
    case AMQP_BASIC_RECOVER_METHOD: return "AMQP_BASIC_RECOVER_METHOD";
    case AMQP_FILE_QOS_METHOD: return "AMQP_FILE_QOS_METHOD";
    case AMQP_FILE_QOS_OK_METHOD: return "AMQP_FILE_QOS_OK_METHOD";
    case AMQP_FILE_CONSUME_METHOD: return "AMQP_FILE_CONSUME_METHOD";
    case AMQP_FILE_CONSUME_OK_METHOD: return "AMQP_FILE_CONSUME_OK_METHOD";
    case AMQP_FILE_CANCEL_METHOD: return "AMQP_FILE_CANCEL_METHOD";
    case AMQP_FILE_CANCEL_OK_METHOD: return "AMQP_FILE_CANCEL_OK_METHOD";
    case AMQP_FILE_OPEN_METHOD: return "AMQP_FILE_OPEN_METHOD";
    case AMQP_FILE_OPEN_OK_METHOD: return "AMQP_FILE_OPEN_OK_METHOD";
    case AMQP_FILE_STAGE_METHOD: return "AMQP_FILE_STAGE_METHOD";
    case AMQP_FILE_PUBLISH_METHOD: return "AMQP_FILE_PUBLISH_METHOD";
    case AMQP_FILE_RETURN_METHOD: return "AMQP_FILE_RETURN_METHOD";
    case AMQP_FILE_DELIVER_METHOD: return "AMQP_FILE_DELIVER_METHOD";
    case AMQP_FILE_ACK_METHOD: return "AMQP_FILE_ACK_METHOD";
    case AMQP_FILE_REJECT_METHOD: return "AMQP_FILE_REJECT_METHOD";
    case AMQP_STREAM_QOS_METHOD: return "AMQP_STREAM_QOS_METHOD";
    case AMQP_STREAM_QOS_OK_METHOD: return "AMQP_STREAM_QOS_OK_METHOD";
    case AMQP_STREAM_CONSUME_METHOD: return "AMQP_STREAM_CONSUME_METHOD";
    case AMQP_STREAM_CONSUME_OK_METHOD: return "AMQP_STREAM_CONSUME_OK_METHOD";
    case AMQP_STREAM_CANCEL_METHOD: return "AMQP_STREAM_CANCEL_METHOD";
    case AMQP_STREAM_CANCEL_OK_METHOD: return "AMQP_STREAM_CANCEL_OK_METHOD";
    case AMQP_STREAM_PUBLISH_METHOD: return "AMQP_STREAM_PUBLISH_METHOD";
    case AMQP_STREAM_RETURN_METHOD: return "AMQP_STREAM_RETURN_METHOD";
    case AMQP_STREAM_DELIVER_METHOD: return "AMQP_STREAM_DELIVER_METHOD";
    case AMQP_TX_SELECT_METHOD: return "AMQP_TX_SELECT_METHOD";
    case AMQP_TX_SELECT_OK_METHOD: return "AMQP_TX_SELECT_OK_METHOD";
    case AMQP_TX_COMMIT_METHOD: return "AMQP_TX_COMMIT_METHOD";
    case AMQP_TX_COMMIT_OK_METHOD: return "AMQP_TX_COMMIT_OK_METHOD";
    case AMQP_TX_ROLLBACK_METHOD: return "AMQP_TX_ROLLBACK_METHOD";
    case AMQP_TX_ROLLBACK_OK_METHOD: return "AMQP_TX_ROLLBACK_OK_METHOD";
    case AMQP_DTX_SELECT_METHOD: return "AMQP_DTX_SELECT_METHOD";
    case AMQP_DTX_SELECT_OK_METHOD: return "AMQP_DTX_SELECT_OK_METHOD";
    case AMQP_DTX_START_METHOD: return "AMQP_DTX_START_METHOD";
    case AMQP_DTX_START_OK_METHOD: return "AMQP_DTX_START_OK_METHOD";
    case AMQP_TUNNEL_REQUEST_METHOD: return "AMQP_TUNNEL_REQUEST_METHOD";
    case AMQP_TEST_INTEGER_METHOD: return "AMQP_TEST_INTEGER_METHOD";
    case AMQP_TEST_INTEGER_OK_METHOD: return "AMQP_TEST_INTEGER_OK_METHOD";
    case AMQP_TEST_STRING_METHOD: return "AMQP_TEST_STRING_METHOD";
    case AMQP_TEST_STRING_OK_METHOD: return "AMQP_TEST_STRING_OK_METHOD";
    case AMQP_TEST_TABLE_METHOD: return "AMQP_TEST_TABLE_METHOD";
    case AMQP_TEST_TABLE_OK_METHOD: return "AMQP_TEST_TABLE_OK_METHOD";
    case AMQP_TEST_CONTENT_METHOD: return "AMQP_TEST_CONTENT_METHOD";
    case AMQP_TEST_CONTENT_OK_METHOD: return "AMQP_TEST_CONTENT_OK_METHOD";
    default: return NULL;
  }
}

amqp_boolean_t amqp_method_has_content(amqp_method_number_t methodNumber) {
  switch (methodNumber) {
    case AMQP_BASIC_PUBLISH_METHOD: return 1;
    case AMQP_BASIC_RETURN_METHOD: return 1;
    case AMQP_BASIC_DELIVER_METHOD: return 1;
    case AMQP_BASIC_GET_OK_METHOD: return 1;
    case AMQP_FILE_STAGE_METHOD: return 1;
    case AMQP_FILE_RETURN_METHOD: return 1;
    case AMQP_STREAM_PUBLISH_METHOD: return 1;
    case AMQP_STREAM_RETURN_METHOD: return 1;
    case AMQP_STREAM_DELIVER_METHOD: return 1;
    case AMQP_TUNNEL_REQUEST_METHOD: return 1;
    case AMQP_TEST_CONTENT_METHOD: return 1;
    case AMQP_TEST_CONTENT_OK_METHOD: return 1;
    default: return 0;
  }
}

int amqp_decode_method(amqp_method_number_t methodNumber,
                       amqp_pool_t *pool,
                       amqp_bytes_t encoded,
                       void **decoded)
{
  int offset = 0;
  int table_result;
  uint8_t bit_buffer;

  switch (methodNumber) {
    case AMQP_CONNECTION_START_METHOD: {
      amqp_connection_start_t *m = (amqp_connection_start_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_start_t));
      m->version_major = D_8(encoded, offset);
      offset++;
      m->version_minor = D_8(encoded, offset);
      offset++;
      table_result = amqp_decode_table(encoded, pool, &(m->server_properties), &offset);
      AMQP_CHECK_RESULT(table_result);
      m->mechanisms.len = D_32(encoded, offset);
      offset += 4;
      m->mechanisms.bytes = D_BYTES(encoded, offset, m->mechanisms.len);
      offset += m->mechanisms.len;
      m->locales.len = D_32(encoded, offset);
      offset += 4;
      m->locales.bytes = D_BYTES(encoded, offset, m->locales.len);
      offset += m->locales.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_START_OK_METHOD: {
      amqp_connection_start_ok_t *m = (amqp_connection_start_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_start_ok_t));
      table_result = amqp_decode_table(encoded, pool, &(m->client_properties), &offset);
      AMQP_CHECK_RESULT(table_result);
      m->mechanism.len = D_8(encoded, offset);
      offset++;
      m->mechanism.bytes = D_BYTES(encoded, offset, m->mechanism.len);
      offset += m->mechanism.len;
      m->response.len = D_32(encoded, offset);
      offset += 4;
      m->response.bytes = D_BYTES(encoded, offset, m->response.len);
      offset += m->response.len;
      m->locale.len = D_8(encoded, offset);
      offset++;
      m->locale.bytes = D_BYTES(encoded, offset, m->locale.len);
      offset += m->locale.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_SECURE_METHOD: {
      amqp_connection_secure_t *m = (amqp_connection_secure_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_secure_t));
      m->challenge.len = D_32(encoded, offset);
      offset += 4;
      m->challenge.bytes = D_BYTES(encoded, offset, m->challenge.len);
      offset += m->challenge.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_SECURE_OK_METHOD: {
      amqp_connection_secure_ok_t *m = (amqp_connection_secure_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_secure_ok_t));
      m->response.len = D_32(encoded, offset);
      offset += 4;
      m->response.bytes = D_BYTES(encoded, offset, m->response.len);
      offset += m->response.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_TUNE_METHOD: {
      amqp_connection_tune_t *m = (amqp_connection_tune_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_tune_t));
      m->channel_max = D_16(encoded, offset);
      offset += 2;
      m->frame_max = D_32(encoded, offset);
      offset += 4;
      m->heartbeat = D_16(encoded, offset);
      offset += 2;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_TUNE_OK_METHOD: {
      amqp_connection_tune_ok_t *m = (amqp_connection_tune_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_tune_ok_t));
      m->channel_max = D_16(encoded, offset);
      offset += 2;
      m->frame_max = D_32(encoded, offset);
      offset += 4;
      m->heartbeat = D_16(encoded, offset);
      offset += 2;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_OPEN_METHOD: {
      amqp_connection_open_t *m = (amqp_connection_open_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_open_t));
      m->virtual_host.len = D_8(encoded, offset);
      offset++;
      m->virtual_host.bytes = D_BYTES(encoded, offset, m->virtual_host.len);
      offset += m->virtual_host.len;
      m->capabilities.len = D_8(encoded, offset);
      offset++;
      m->capabilities.bytes = D_BYTES(encoded, offset, m->capabilities.len);
      offset += m->capabilities.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->insist = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_OPEN_OK_METHOD: {
      amqp_connection_open_ok_t *m = (amqp_connection_open_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_open_ok_t));
      m->known_hosts.len = D_8(encoded, offset);
      offset++;
      m->known_hosts.bytes = D_BYTES(encoded, offset, m->known_hosts.len);
      offset += m->known_hosts.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_REDIRECT_METHOD: {
      amqp_connection_redirect_t *m = (amqp_connection_redirect_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_redirect_t));
      m->host.len = D_8(encoded, offset);
      offset++;
      m->host.bytes = D_BYTES(encoded, offset, m->host.len);
      offset += m->host.len;
      m->known_hosts.len = D_8(encoded, offset);
      offset++;
      m->known_hosts.bytes = D_BYTES(encoded, offset, m->known_hosts.len);
      offset += m->known_hosts.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_CLOSE_METHOD: {
      amqp_connection_close_t *m = (amqp_connection_close_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_close_t));
      m->reply_code = D_16(encoded, offset);
      offset += 2;
      m->reply_text.len = D_8(encoded, offset);
      offset++;
      m->reply_text.bytes = D_BYTES(encoded, offset, m->reply_text.len);
      offset += m->reply_text.len;
      m->class_id = D_16(encoded, offset);
      offset += 2;
      m->method_id = D_16(encoded, offset);
      offset += 2;
      *decoded = m;
      return 0;
    }
    case AMQP_CONNECTION_CLOSE_OK_METHOD: {
      amqp_connection_close_ok_t *m = (amqp_connection_close_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_close_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_OPEN_METHOD: {
      amqp_channel_open_t *m = (amqp_channel_open_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_open_t));
      m->out_of_band.len = D_8(encoded, offset);
      offset++;
      m->out_of_band.bytes = D_BYTES(encoded, offset, m->out_of_band.len);
      offset += m->out_of_band.len;
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_OPEN_OK_METHOD: {
      amqp_channel_open_ok_t *m = (amqp_channel_open_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_open_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_FLOW_METHOD: {
      amqp_channel_flow_t *m = (amqp_channel_flow_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_flow_t));
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->active = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_FLOW_OK_METHOD: {
      amqp_channel_flow_ok_t *m = (amqp_channel_flow_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_flow_ok_t));
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->active = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_ALERT_METHOD: {
      amqp_channel_alert_t *m = (amqp_channel_alert_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_alert_t));
      m->reply_code = D_16(encoded, offset);
      offset += 2;
      m->reply_text.len = D_8(encoded, offset);
      offset++;
      m->reply_text.bytes = D_BYTES(encoded, offset, m->reply_text.len);
      offset += m->reply_text.len;
      table_result = amqp_decode_table(encoded, pool, &(m->details), &offset);
      AMQP_CHECK_RESULT(table_result);
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_CLOSE_METHOD: {
      amqp_channel_close_t *m = (amqp_channel_close_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_close_t));
      m->reply_code = D_16(encoded, offset);
      offset += 2;
      m->reply_text.len = D_8(encoded, offset);
      offset++;
      m->reply_text.bytes = D_BYTES(encoded, offset, m->reply_text.len);
      offset += m->reply_text.len;
      m->class_id = D_16(encoded, offset);
      offset += 2;
      m->method_id = D_16(encoded, offset);
      offset += 2;
      *decoded = m;
      return 0;
    }
    case AMQP_CHANNEL_CLOSE_OK_METHOD: {
      amqp_channel_close_ok_t *m = (amqp_channel_close_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_close_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_ACCESS_REQUEST_METHOD: {
      amqp_access_request_t *m = (amqp_access_request_t *) amqp_pool_alloc(pool, sizeof(amqp_access_request_t));
      m->realm.len = D_8(encoded, offset);
      offset++;
      m->realm.bytes = D_BYTES(encoded, offset, m->realm.len);
      offset += m->realm.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->exclusive = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->passive = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->active = (bit_buffer & (1 << 2)) ? 1 : 0;
      m->write = (bit_buffer & (1 << 3)) ? 1 : 0;
      m->read = (bit_buffer & (1 << 4)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_ACCESS_REQUEST_OK_METHOD: {
      amqp_access_request_ok_t *m = (amqp_access_request_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_access_request_ok_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      *decoded = m;
      return 0;
    }
    case AMQP_EXCHANGE_DECLARE_METHOD: {
      amqp_exchange_declare_t *m = (amqp_exchange_declare_t *) amqp_pool_alloc(pool, sizeof(amqp_exchange_declare_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->type.len = D_8(encoded, offset);
      offset++;
      m->type.bytes = D_BYTES(encoded, offset, m->type.len);
      offset += m->type.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->passive = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->durable = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->auto_delete = (bit_buffer & (1 << 2)) ? 1 : 0;
      m->internal = (bit_buffer & (1 << 3)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 4)) ? 1 : 0;
      table_result = amqp_decode_table(encoded, pool, &(m->arguments), &offset);
      AMQP_CHECK_RESULT(table_result);
      *decoded = m;
      return 0;
    }
    case AMQP_EXCHANGE_DECLARE_OK_METHOD: {
      amqp_exchange_declare_ok_t *m = (amqp_exchange_declare_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_exchange_declare_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_EXCHANGE_DELETE_METHOD: {
      amqp_exchange_delete_t *m = (amqp_exchange_delete_t *) amqp_pool_alloc(pool, sizeof(amqp_exchange_delete_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->if_unused = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 1)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_EXCHANGE_DELETE_OK_METHOD: {
      amqp_exchange_delete_ok_t *m = (amqp_exchange_delete_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_exchange_delete_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_DECLARE_METHOD: {
      amqp_queue_declare_t *m = (amqp_queue_declare_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_declare_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->passive = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->durable = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->exclusive = (bit_buffer & (1 << 2)) ? 1 : 0;
      m->auto_delete = (bit_buffer & (1 << 3)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 4)) ? 1 : 0;
      table_result = amqp_decode_table(encoded, pool, &(m->arguments), &offset);
      AMQP_CHECK_RESULT(table_result);
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_DECLARE_OK_METHOD: {
      amqp_queue_declare_ok_t *m = (amqp_queue_declare_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_declare_ok_t));
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      m->message_count = D_32(encoded, offset);
      offset += 4;
      m->consumer_count = D_32(encoded, offset);
      offset += 4;
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_BIND_METHOD: {
      amqp_queue_bind_t *m = (amqp_queue_bind_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_bind_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->nowait = (bit_buffer & (1 << 0)) ? 1 : 0;
      table_result = amqp_decode_table(encoded, pool, &(m->arguments), &offset);
      AMQP_CHECK_RESULT(table_result);
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_BIND_OK_METHOD: {
      amqp_queue_bind_ok_t *m = (amqp_queue_bind_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_bind_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_PURGE_METHOD: {
      amqp_queue_purge_t *m = (amqp_queue_purge_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_purge_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->nowait = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_PURGE_OK_METHOD: {
      amqp_queue_purge_ok_t *m = (amqp_queue_purge_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_purge_ok_t));
      m->message_count = D_32(encoded, offset);
      offset += 4;
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_DELETE_METHOD: {
      amqp_queue_delete_t *m = (amqp_queue_delete_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_delete_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->if_unused = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->if_empty = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 2)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_DELETE_OK_METHOD: {
      amqp_queue_delete_ok_t *m = (amqp_queue_delete_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_delete_ok_t));
      m->message_count = D_32(encoded, offset);
      offset += 4;
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_UNBIND_METHOD: {
      amqp_queue_unbind_t *m = (amqp_queue_unbind_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_unbind_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      table_result = amqp_decode_table(encoded, pool, &(m->arguments), &offset);
      AMQP_CHECK_RESULT(table_result);
      *decoded = m;
      return 0;
    }
    case AMQP_QUEUE_UNBIND_OK_METHOD: {
      amqp_queue_unbind_ok_t *m = (amqp_queue_unbind_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_unbind_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_QOS_METHOD: {
      amqp_basic_qos_t *m = (amqp_basic_qos_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_qos_t));
      m->prefetch_size = D_32(encoded, offset);
      offset += 4;
      m->prefetch_count = D_16(encoded, offset);
      offset += 2;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->global = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_QOS_OK_METHOD: {
      amqp_basic_qos_ok_t *m = (amqp_basic_qos_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_qos_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_CONSUME_METHOD: {
      amqp_basic_consume_t *m = (amqp_basic_consume_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_consume_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->no_local = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->no_ack = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->exclusive = (bit_buffer & (1 << 2)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 3)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_CONSUME_OK_METHOD: {
      amqp_basic_consume_ok_t *m = (amqp_basic_consume_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_consume_ok_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_CANCEL_METHOD: {
      amqp_basic_cancel_t *m = (amqp_basic_cancel_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_cancel_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->nowait = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_CANCEL_OK_METHOD: {
      amqp_basic_cancel_ok_t *m = (amqp_basic_cancel_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_cancel_ok_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_PUBLISH_METHOD: {
      amqp_basic_publish_t *m = (amqp_basic_publish_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_publish_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->mandatory = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->immediate = (bit_buffer & (1 << 1)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_RETURN_METHOD: {
      amqp_basic_return_t *m = (amqp_basic_return_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_return_t));
      m->reply_code = D_16(encoded, offset);
      offset += 2;
      m->reply_text.len = D_8(encoded, offset);
      offset++;
      m->reply_text.bytes = D_BYTES(encoded, offset, m->reply_text.len);
      offset += m->reply_text.len;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_DELIVER_METHOD: {
      amqp_basic_deliver_t *m = (amqp_basic_deliver_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_deliver_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->redelivered = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_GET_METHOD: {
      amqp_basic_get_t *m = (amqp_basic_get_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_get_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->no_ack = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_GET_OK_METHOD: {
      amqp_basic_get_ok_t *m = (amqp_basic_get_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_get_ok_t));
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->redelivered = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      m->message_count = D_32(encoded, offset);
      offset += 4;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_GET_EMPTY_METHOD: {
      amqp_basic_get_empty_t *m = (amqp_basic_get_empty_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_get_empty_t));
      m->cluster_id.len = D_8(encoded, offset);
      offset++;
      m->cluster_id.bytes = D_BYTES(encoded, offset, m->cluster_id.len);
      offset += m->cluster_id.len;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_ACK_METHOD: {
      amqp_basic_ack_t *m = (amqp_basic_ack_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_ack_t));
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->multiple = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_REJECT_METHOD: {
      amqp_basic_reject_t *m = (amqp_basic_reject_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_reject_t));
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->requeue = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_BASIC_RECOVER_METHOD: {
      amqp_basic_recover_t *m = (amqp_basic_recover_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_recover_t));
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->requeue = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_QOS_METHOD: {
      amqp_file_qos_t *m = (amqp_file_qos_t *) amqp_pool_alloc(pool, sizeof(amqp_file_qos_t));
      m->prefetch_size = D_32(encoded, offset);
      offset += 4;
      m->prefetch_count = D_16(encoded, offset);
      offset += 2;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->global = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_QOS_OK_METHOD: {
      amqp_file_qos_ok_t *m = (amqp_file_qos_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_file_qos_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_CONSUME_METHOD: {
      amqp_file_consume_t *m = (amqp_file_consume_t *) amqp_pool_alloc(pool, sizeof(amqp_file_consume_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->no_local = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->no_ack = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->exclusive = (bit_buffer & (1 << 2)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 3)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_CONSUME_OK_METHOD: {
      amqp_file_consume_ok_t *m = (amqp_file_consume_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_file_consume_ok_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_CANCEL_METHOD: {
      amqp_file_cancel_t *m = (amqp_file_cancel_t *) amqp_pool_alloc(pool, sizeof(amqp_file_cancel_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->nowait = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_CANCEL_OK_METHOD: {
      amqp_file_cancel_ok_t *m = (amqp_file_cancel_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_file_cancel_ok_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_OPEN_METHOD: {
      amqp_file_open_t *m = (amqp_file_open_t *) amqp_pool_alloc(pool, sizeof(amqp_file_open_t));
      m->identifier.len = D_8(encoded, offset);
      offset++;
      m->identifier.bytes = D_BYTES(encoded, offset, m->identifier.len);
      offset += m->identifier.len;
      m->content_size = D_64(encoded, offset);
      offset += 8;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_OPEN_OK_METHOD: {
      amqp_file_open_ok_t *m = (amqp_file_open_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_file_open_ok_t));
      m->staged_size = D_64(encoded, offset);
      offset += 8;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_STAGE_METHOD: {
      amqp_file_stage_t *m = (amqp_file_stage_t *) amqp_pool_alloc(pool, sizeof(amqp_file_stage_t));
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_PUBLISH_METHOD: {
      amqp_file_publish_t *m = (amqp_file_publish_t *) amqp_pool_alloc(pool, sizeof(amqp_file_publish_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->mandatory = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->immediate = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->identifier.len = D_8(encoded, offset);
      offset++;
      m->identifier.bytes = D_BYTES(encoded, offset, m->identifier.len);
      offset += m->identifier.len;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_RETURN_METHOD: {
      amqp_file_return_t *m = (amqp_file_return_t *) amqp_pool_alloc(pool, sizeof(amqp_file_return_t));
      m->reply_code = D_16(encoded, offset);
      offset += 2;
      m->reply_text.len = D_8(encoded, offset);
      offset++;
      m->reply_text.bytes = D_BYTES(encoded, offset, m->reply_text.len);
      offset += m->reply_text.len;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_DELIVER_METHOD: {
      amqp_file_deliver_t *m = (amqp_file_deliver_t *) amqp_pool_alloc(pool, sizeof(amqp_file_deliver_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->redelivered = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      m->identifier.len = D_8(encoded, offset);
      offset++;
      m->identifier.bytes = D_BYTES(encoded, offset, m->identifier.len);
      offset += m->identifier.len;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_ACK_METHOD: {
      amqp_file_ack_t *m = (amqp_file_ack_t *) amqp_pool_alloc(pool, sizeof(amqp_file_ack_t));
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->multiple = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_FILE_REJECT_METHOD: {
      amqp_file_reject_t *m = (amqp_file_reject_t *) amqp_pool_alloc(pool, sizeof(amqp_file_reject_t));
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->requeue = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_QOS_METHOD: {
      amqp_stream_qos_t *m = (amqp_stream_qos_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_qos_t));
      m->prefetch_size = D_32(encoded, offset);
      offset += 4;
      m->prefetch_count = D_16(encoded, offset);
      offset += 2;
      m->consume_rate = D_32(encoded, offset);
      offset += 4;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->global = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_QOS_OK_METHOD: {
      amqp_stream_qos_ok_t *m = (amqp_stream_qos_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_qos_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_CONSUME_METHOD: {
      amqp_stream_consume_t *m = (amqp_stream_consume_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_consume_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->no_local = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->exclusive = (bit_buffer & (1 << 1)) ? 1 : 0;
      m->nowait = (bit_buffer & (1 << 2)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_CONSUME_OK_METHOD: {
      amqp_stream_consume_ok_t *m = (amqp_stream_consume_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_consume_ok_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_CANCEL_METHOD: {
      amqp_stream_cancel_t *m = (amqp_stream_cancel_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_cancel_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->nowait = (bit_buffer & (1 << 0)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_CANCEL_OK_METHOD: {
      amqp_stream_cancel_ok_t *m = (amqp_stream_cancel_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_cancel_ok_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_PUBLISH_METHOD: {
      amqp_stream_publish_t *m = (amqp_stream_publish_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_publish_t));
      m->ticket = D_16(encoded, offset);
      offset += 2;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      bit_buffer = D_8(encoded, offset);
      offset++;
      m->mandatory = (bit_buffer & (1 << 0)) ? 1 : 0;
      m->immediate = (bit_buffer & (1 << 1)) ? 1 : 0;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_RETURN_METHOD: {
      amqp_stream_return_t *m = (amqp_stream_return_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_return_t));
      m->reply_code = D_16(encoded, offset);
      offset += 2;
      m->reply_text.len = D_8(encoded, offset);
      offset++;
      m->reply_text.bytes = D_BYTES(encoded, offset, m->reply_text.len);
      offset += m->reply_text.len;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->routing_key.len = D_8(encoded, offset);
      offset++;
      m->routing_key.bytes = D_BYTES(encoded, offset, m->routing_key.len);
      offset += m->routing_key.len;
      *decoded = m;
      return 0;
    }
    case AMQP_STREAM_DELIVER_METHOD: {
      amqp_stream_deliver_t *m = (amqp_stream_deliver_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_deliver_t));
      m->consumer_tag.len = D_8(encoded, offset);
      offset++;
      m->consumer_tag.bytes = D_BYTES(encoded, offset, m->consumer_tag.len);
      offset += m->consumer_tag.len;
      m->delivery_tag = D_64(encoded, offset);
      offset += 8;
      m->exchange.len = D_8(encoded, offset);
      offset++;
      m->exchange.bytes = D_BYTES(encoded, offset, m->exchange.len);
      offset += m->exchange.len;
      m->queue.len = D_8(encoded, offset);
      offset++;
      m->queue.bytes = D_BYTES(encoded, offset, m->queue.len);
      offset += m->queue.len;
      *decoded = m;
      return 0;
    }
    case AMQP_TX_SELECT_METHOD: {
      amqp_tx_select_t *m = (amqp_tx_select_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_select_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TX_SELECT_OK_METHOD: {
      amqp_tx_select_ok_t *m = (amqp_tx_select_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_select_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TX_COMMIT_METHOD: {
      amqp_tx_commit_t *m = (amqp_tx_commit_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_commit_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TX_COMMIT_OK_METHOD: {
      amqp_tx_commit_ok_t *m = (amqp_tx_commit_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_commit_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TX_ROLLBACK_METHOD: {
      amqp_tx_rollback_t *m = (amqp_tx_rollback_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_rollback_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TX_ROLLBACK_OK_METHOD: {
      amqp_tx_rollback_ok_t *m = (amqp_tx_rollback_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_rollback_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_DTX_SELECT_METHOD: {
      amqp_dtx_select_t *m = (amqp_dtx_select_t *) amqp_pool_alloc(pool, sizeof(amqp_dtx_select_t));
      *decoded = m;
      return 0;
    }
    case AMQP_DTX_SELECT_OK_METHOD: {
      amqp_dtx_select_ok_t *m = (amqp_dtx_select_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_dtx_select_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_DTX_START_METHOD: {
      amqp_dtx_start_t *m = (amqp_dtx_start_t *) amqp_pool_alloc(pool, sizeof(amqp_dtx_start_t));
      m->dtx_identifier.len = D_8(encoded, offset);
      offset++;
      m->dtx_identifier.bytes = D_BYTES(encoded, offset, m->dtx_identifier.len);
      offset += m->dtx_identifier.len;
      *decoded = m;
      return 0;
    }
    case AMQP_DTX_START_OK_METHOD: {
      amqp_dtx_start_ok_t *m = (amqp_dtx_start_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_dtx_start_ok_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TUNNEL_REQUEST_METHOD: {
      amqp_tunnel_request_t *m = (amqp_tunnel_request_t *) amqp_pool_alloc(pool, sizeof(amqp_tunnel_request_t));
      table_result = amqp_decode_table(encoded, pool, &(m->meta_data), &offset);
      AMQP_CHECK_RESULT(table_result);
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_INTEGER_METHOD: {
      amqp_test_integer_t *m = (amqp_test_integer_t *) amqp_pool_alloc(pool, sizeof(amqp_test_integer_t));
      m->integer_1 = D_8(encoded, offset);
      offset++;
      m->integer_2 = D_16(encoded, offset);
      offset += 2;
      m->integer_3 = D_32(encoded, offset);
      offset += 4;
      m->integer_4 = D_64(encoded, offset);
      offset += 8;
      m->operation = D_8(encoded, offset);
      offset++;
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_INTEGER_OK_METHOD: {
      amqp_test_integer_ok_t *m = (amqp_test_integer_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_test_integer_ok_t));
      m->result = D_64(encoded, offset);
      offset += 8;
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_STRING_METHOD: {
      amqp_test_string_t *m = (amqp_test_string_t *) amqp_pool_alloc(pool, sizeof(amqp_test_string_t));
      m->string_1.len = D_8(encoded, offset);
      offset++;
      m->string_1.bytes = D_BYTES(encoded, offset, m->string_1.len);
      offset += m->string_1.len;
      m->string_2.len = D_32(encoded, offset);
      offset += 4;
      m->string_2.bytes = D_BYTES(encoded, offset, m->string_2.len);
      offset += m->string_2.len;
      m->operation = D_8(encoded, offset);
      offset++;
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_STRING_OK_METHOD: {
      amqp_test_string_ok_t *m = (amqp_test_string_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_test_string_ok_t));
      m->result.len = D_32(encoded, offset);
      offset += 4;
      m->result.bytes = D_BYTES(encoded, offset, m->result.len);
      offset += m->result.len;
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_TABLE_METHOD: {
      amqp_test_table_t *m = (amqp_test_table_t *) amqp_pool_alloc(pool, sizeof(amqp_test_table_t));
      table_result = amqp_decode_table(encoded, pool, &(m->table), &offset);
      AMQP_CHECK_RESULT(table_result);
      m->integer_op = D_8(encoded, offset);
      offset++;
      m->string_op = D_8(encoded, offset);
      offset++;
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_TABLE_OK_METHOD: {
      amqp_test_table_ok_t *m = (amqp_test_table_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_test_table_ok_t));
      m->integer_result = D_64(encoded, offset);
      offset += 8;
      m->string_result.len = D_32(encoded, offset);
      offset += 4;
      m->string_result.bytes = D_BYTES(encoded, offset, m->string_result.len);
      offset += m->string_result.len;
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_CONTENT_METHOD: {
      amqp_test_content_t *m = (amqp_test_content_t *) amqp_pool_alloc(pool, sizeof(amqp_test_content_t));
      *decoded = m;
      return 0;
    }
    case AMQP_TEST_CONTENT_OK_METHOD: {
      amqp_test_content_ok_t *m = (amqp_test_content_ok_t *) amqp_pool_alloc(pool, sizeof(amqp_test_content_ok_t));
      m->content_checksum = D_32(encoded, offset);
      offset += 4;
      *decoded = m;
      return 0;
    }
    default: return -ENOENT;
  }
}

int amqp_decode_properties(uint16_t class_id,
                           amqp_pool_t *pool,
                           amqp_bytes_t encoded,
                           void **decoded)
{
  int offset = 0;
  int table_result;

  amqp_flags_t flags = 0;
  int flagword_index = 0;
  amqp_flags_t partial_flags;

  do {
    partial_flags = D_16(encoded, offset);
    offset += 2;
    flags |= (partial_flags << (flagword_index * 16));
    flagword_index++;
  } while (partial_flags & 1);

  switch (class_id) {
    case 10: {
      amqp_connection_properties_t *p = (amqp_connection_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_connection_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 20: {
      amqp_channel_properties_t *p = (amqp_channel_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_channel_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 30: {
      amqp_access_properties_t *p = (amqp_access_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_access_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 40: {
      amqp_exchange_properties_t *p = (amqp_exchange_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_exchange_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 50: {
      amqp_queue_properties_t *p = (amqp_queue_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_queue_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 60: {
      amqp_basic_properties_t *p = (amqp_basic_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_basic_properties_t));
      p->_flags = flags;
      if (flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
        p->content_type.len = D_8(encoded, offset);
        offset++;
        p->content_type.bytes = D_BYTES(encoded, offset, p->content_type.len);
        offset += p->content_type.len;
      }
      if (flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
        p->content_encoding.len = D_8(encoded, offset);
        offset++;
        p->content_encoding.bytes = D_BYTES(encoded, offset, p->content_encoding.len);
        offset += p->content_encoding.len;
      }
      if (flags & AMQP_BASIC_HEADERS_FLAG) {
        table_result = amqp_decode_table(encoded, pool, &(p->headers), &offset);
        AMQP_CHECK_RESULT(table_result);
      }
      if (flags & AMQP_BASIC_DELIVERY_MODE_FLAG) {
        p->delivery_mode = D_8(encoded, offset);
        offset++;
      }
      if (flags & AMQP_BASIC_PRIORITY_FLAG) {
        p->priority = D_8(encoded, offset);
        offset++;
      }
      if (flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        p->correlation_id.len = D_8(encoded, offset);
        offset++;
        p->correlation_id.bytes = D_BYTES(encoded, offset, p->correlation_id.len);
        offset += p->correlation_id.len;
      }
      if (flags & AMQP_BASIC_REPLY_TO_FLAG) {
        p->reply_to.len = D_8(encoded, offset);
        offset++;
        p->reply_to.bytes = D_BYTES(encoded, offset, p->reply_to.len);
        offset += p->reply_to.len;
      }
      if (flags & AMQP_BASIC_EXPIRATION_FLAG) {
        p->expiration.len = D_8(encoded, offset);
        offset++;
        p->expiration.bytes = D_BYTES(encoded, offset, p->expiration.len);
        offset += p->expiration.len;
      }
      if (flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
        p->message_id.len = D_8(encoded, offset);
        offset++;
        p->message_id.bytes = D_BYTES(encoded, offset, p->message_id.len);
        offset += p->message_id.len;
      }
      if (flags & AMQP_BASIC_TIMESTAMP_FLAG) {
        p->timestamp = D_64(encoded, offset);
        offset += 8;
      }
      if (flags & AMQP_BASIC_TYPE_FLAG) {
        p->type.len = D_8(encoded, offset);
        offset++;
        p->type.bytes = D_BYTES(encoded, offset, p->type.len);
        offset += p->type.len;
      }
      if (flags & AMQP_BASIC_USER_ID_FLAG) {
        p->user_id.len = D_8(encoded, offset);
        offset++;
        p->user_id.bytes = D_BYTES(encoded, offset, p->user_id.len);
        offset += p->user_id.len;
      }
      if (flags & AMQP_BASIC_APP_ID_FLAG) {
        p->app_id.len = D_8(encoded, offset);
        offset++;
        p->app_id.bytes = D_BYTES(encoded, offset, p->app_id.len);
        offset += p->app_id.len;
      }
      if (flags & AMQP_BASIC_CLUSTER_ID_FLAG) {
        p->cluster_id.len = D_8(encoded, offset);
        offset++;
        p->cluster_id.bytes = D_BYTES(encoded, offset, p->cluster_id.len);
        offset += p->cluster_id.len;
      }
      *decoded = p;
      return 0;
    }
    case 70: {
      amqp_file_properties_t *p = (amqp_file_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_file_properties_t));
      p->_flags = flags;
      if (flags & AMQP_FILE_CONTENT_TYPE_FLAG) {
        p->content_type.len = D_8(encoded, offset);
        offset++;
        p->content_type.bytes = D_BYTES(encoded, offset, p->content_type.len);
        offset += p->content_type.len;
      }
      if (flags & AMQP_FILE_CONTENT_ENCODING_FLAG) {
        p->content_encoding.len = D_8(encoded, offset);
        offset++;
        p->content_encoding.bytes = D_BYTES(encoded, offset, p->content_encoding.len);
        offset += p->content_encoding.len;
      }
      if (flags & AMQP_FILE_HEADERS_FLAG) {
        table_result = amqp_decode_table(encoded, pool, &(p->headers), &offset);
        AMQP_CHECK_RESULT(table_result);
      }
      if (flags & AMQP_FILE_PRIORITY_FLAG) {
        p->priority = D_8(encoded, offset);
        offset++;
      }
      if (flags & AMQP_FILE_REPLY_TO_FLAG) {
        p->reply_to.len = D_8(encoded, offset);
        offset++;
        p->reply_to.bytes = D_BYTES(encoded, offset, p->reply_to.len);
        offset += p->reply_to.len;
      }
      if (flags & AMQP_FILE_MESSAGE_ID_FLAG) {
        p->message_id.len = D_8(encoded, offset);
        offset++;
        p->message_id.bytes = D_BYTES(encoded, offset, p->message_id.len);
        offset += p->message_id.len;
      }
      if (flags & AMQP_FILE_FILENAME_FLAG) {
        p->filename.len = D_8(encoded, offset);
        offset++;
        p->filename.bytes = D_BYTES(encoded, offset, p->filename.len);
        offset += p->filename.len;
      }
      if (flags & AMQP_FILE_TIMESTAMP_FLAG) {
        p->timestamp = D_64(encoded, offset);
        offset += 8;
      }
      if (flags & AMQP_FILE_CLUSTER_ID_FLAG) {
        p->cluster_id.len = D_8(encoded, offset);
        offset++;
        p->cluster_id.bytes = D_BYTES(encoded, offset, p->cluster_id.len);
        offset += p->cluster_id.len;
      }
      *decoded = p;
      return 0;
    }
    case 80: {
      amqp_stream_properties_t *p = (amqp_stream_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_stream_properties_t));
      p->_flags = flags;
      if (flags & AMQP_STREAM_CONTENT_TYPE_FLAG) {
        p->content_type.len = D_8(encoded, offset);
        offset++;
        p->content_type.bytes = D_BYTES(encoded, offset, p->content_type.len);
        offset += p->content_type.len;
      }
      if (flags & AMQP_STREAM_CONTENT_ENCODING_FLAG) {
        p->content_encoding.len = D_8(encoded, offset);
        offset++;
        p->content_encoding.bytes = D_BYTES(encoded, offset, p->content_encoding.len);
        offset += p->content_encoding.len;
      }
      if (flags & AMQP_STREAM_HEADERS_FLAG) {
        table_result = amqp_decode_table(encoded, pool, &(p->headers), &offset);
        AMQP_CHECK_RESULT(table_result);
      }
      if (flags & AMQP_STREAM_PRIORITY_FLAG) {
        p->priority = D_8(encoded, offset);
        offset++;
      }
      if (flags & AMQP_STREAM_TIMESTAMP_FLAG) {
        p->timestamp = D_64(encoded, offset);
        offset += 8;
      }
      *decoded = p;
      return 0;
    }
    case 90: {
      amqp_tx_properties_t *p = (amqp_tx_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_tx_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 100: {
      amqp_dtx_properties_t *p = (amqp_dtx_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_dtx_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    case 110: {
      amqp_tunnel_properties_t *p = (amqp_tunnel_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_tunnel_properties_t));
      p->_flags = flags;
      if (flags & AMQP_TUNNEL_HEADERS_FLAG) {
        table_result = amqp_decode_table(encoded, pool, &(p->headers), &offset);
        AMQP_CHECK_RESULT(table_result);
      }
      if (flags & AMQP_TUNNEL_PROXY_NAME_FLAG) {
        p->proxy_name.len = D_8(encoded, offset);
        offset++;
        p->proxy_name.bytes = D_BYTES(encoded, offset, p->proxy_name.len);
        offset += p->proxy_name.len;
      }
      if (flags & AMQP_TUNNEL_DATA_NAME_FLAG) {
        p->data_name.len = D_8(encoded, offset);
        offset++;
        p->data_name.bytes = D_BYTES(encoded, offset, p->data_name.len);
        offset += p->data_name.len;
      }
      if (flags & AMQP_TUNNEL_DURABLE_FLAG) {
        p->durable = D_8(encoded, offset);
        offset++;
      }
      if (flags & AMQP_TUNNEL_BROADCAST_FLAG) {
        p->broadcast = D_8(encoded, offset);
        offset++;
      }
      *decoded = p;
      return 0;
    }
    case 120: {
      amqp_test_properties_t *p = (amqp_test_properties_t *) amqp_pool_alloc(pool, sizeof(amqp_test_properties_t));
      p->_flags = flags;
      *decoded = p;
      return 0;
    }
    default: return -ENOENT;
  }
}

int amqp_encode_method(amqp_method_number_t methodNumber,
                       void *decoded,
                       amqp_bytes_t encoded)
{
  int offset = 0;
  int table_result;
  uint8_t bit_buffer;

  switch (methodNumber) {
    case AMQP_CONNECTION_START_METHOD: {
      amqp_connection_start_t *m = (amqp_connection_start_t *) decoded;
      E_8(encoded, offset, m->version_major);
      offset++;
      E_8(encoded, offset, m->version_minor);
      offset++;
      table_result = amqp_encode_table(encoded, &(m->server_properties), &offset);
      if (table_result < 0) return table_result;
      E_32(encoded, offset, m->mechanisms.len);
      offset += 4;
      E_BYTES(encoded, offset, m->mechanisms.len, m->mechanisms.bytes);
      offset += m->mechanisms.len;
      E_32(encoded, offset, m->locales.len);
      offset += 4;
      E_BYTES(encoded, offset, m->locales.len, m->locales.bytes);
      offset += m->locales.len;
      return offset;
    }
    case AMQP_CONNECTION_START_OK_METHOD: {
      amqp_connection_start_ok_t *m = (amqp_connection_start_ok_t *) decoded;
      table_result = amqp_encode_table(encoded, &(m->client_properties), &offset);
      if (table_result < 0) return table_result;
      E_8(encoded, offset, m->mechanism.len);
      offset++;
      E_BYTES(encoded, offset, m->mechanism.len, m->mechanism.bytes);
      offset += m->mechanism.len;
      E_32(encoded, offset, m->response.len);
      offset += 4;
      E_BYTES(encoded, offset, m->response.len, m->response.bytes);
      offset += m->response.len;
      E_8(encoded, offset, m->locale.len);
      offset++;
      E_BYTES(encoded, offset, m->locale.len, m->locale.bytes);
      offset += m->locale.len;
      return offset;
    }
    case AMQP_CONNECTION_SECURE_METHOD: {
      amqp_connection_secure_t *m = (amqp_connection_secure_t *) decoded;
      E_32(encoded, offset, m->challenge.len);
      offset += 4;
      E_BYTES(encoded, offset, m->challenge.len, m->challenge.bytes);
      offset += m->challenge.len;
      return offset;
    }
    case AMQP_CONNECTION_SECURE_OK_METHOD: {
      amqp_connection_secure_ok_t *m = (amqp_connection_secure_ok_t *) decoded;
      E_32(encoded, offset, m->response.len);
      offset += 4;
      E_BYTES(encoded, offset, m->response.len, m->response.bytes);
      offset += m->response.len;
      return offset;
    }
    case AMQP_CONNECTION_TUNE_METHOD: {
      amqp_connection_tune_t *m = (amqp_connection_tune_t *) decoded;
      E_16(encoded, offset, m->channel_max);
      offset += 2;
      E_32(encoded, offset, m->frame_max);
      offset += 4;
      E_16(encoded, offset, m->heartbeat);
      offset += 2;
      return offset;
    }
    case AMQP_CONNECTION_TUNE_OK_METHOD: {
      amqp_connection_tune_ok_t *m = (amqp_connection_tune_ok_t *) decoded;
      E_16(encoded, offset, m->channel_max);
      offset += 2;
      E_32(encoded, offset, m->frame_max);
      offset += 4;
      E_16(encoded, offset, m->heartbeat);
      offset += 2;
      return offset;
    }
    case AMQP_CONNECTION_OPEN_METHOD: {
      amqp_connection_open_t *m = (amqp_connection_open_t *) decoded;
      E_8(encoded, offset, m->virtual_host.len);
      offset++;
      E_BYTES(encoded, offset, m->virtual_host.len, m->virtual_host.bytes);
      offset += m->virtual_host.len;
      E_8(encoded, offset, m->capabilities.len);
      offset++;
      E_BYTES(encoded, offset, m->capabilities.len, m->capabilities.bytes);
      offset += m->capabilities.len;
      bit_buffer = 0;
      if (m->insist) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_CONNECTION_OPEN_OK_METHOD: {
      amqp_connection_open_ok_t *m = (amqp_connection_open_ok_t *) decoded;
      E_8(encoded, offset, m->known_hosts.len);
      offset++;
      E_BYTES(encoded, offset, m->known_hosts.len, m->known_hosts.bytes);
      offset += m->known_hosts.len;
      return offset;
    }
    case AMQP_CONNECTION_REDIRECT_METHOD: {
      amqp_connection_redirect_t *m = (amqp_connection_redirect_t *) decoded;
      E_8(encoded, offset, m->host.len);
      offset++;
      E_BYTES(encoded, offset, m->host.len, m->host.bytes);
      offset += m->host.len;
      E_8(encoded, offset, m->known_hosts.len);
      offset++;
      E_BYTES(encoded, offset, m->known_hosts.len, m->known_hosts.bytes);
      offset += m->known_hosts.len;
      return offset;
    }
    case AMQP_CONNECTION_CLOSE_METHOD: {
      amqp_connection_close_t *m = (amqp_connection_close_t *) decoded;
      E_16(encoded, offset, m->reply_code);
      offset += 2;
      E_8(encoded, offset, m->reply_text.len);
      offset++;
      E_BYTES(encoded, offset, m->reply_text.len, m->reply_text.bytes);
      offset += m->reply_text.len;
      E_16(encoded, offset, m->class_id);
      offset += 2;
      E_16(encoded, offset, m->method_id);
      offset += 2;
      return offset;
    }
    case AMQP_CONNECTION_CLOSE_OK_METHOD: {
      return offset;
    }
    case AMQP_CHANNEL_OPEN_METHOD: {
      amqp_channel_open_t *m = (amqp_channel_open_t *) decoded;
      E_8(encoded, offset, m->out_of_band.len);
      offset++;
      E_BYTES(encoded, offset, m->out_of_band.len, m->out_of_band.bytes);
      offset += m->out_of_band.len;
      return offset;
    }
    case AMQP_CHANNEL_OPEN_OK_METHOD: {
      return offset;
    }
    case AMQP_CHANNEL_FLOW_METHOD: {
      amqp_channel_flow_t *m = (amqp_channel_flow_t *) decoded;
      bit_buffer = 0;
      if (m->active) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_CHANNEL_FLOW_OK_METHOD: {
      amqp_channel_flow_ok_t *m = (amqp_channel_flow_ok_t *) decoded;
      bit_buffer = 0;
      if (m->active) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_CHANNEL_ALERT_METHOD: {
      amqp_channel_alert_t *m = (amqp_channel_alert_t *) decoded;
      E_16(encoded, offset, m->reply_code);
      offset += 2;
      E_8(encoded, offset, m->reply_text.len);
      offset++;
      E_BYTES(encoded, offset, m->reply_text.len, m->reply_text.bytes);
      offset += m->reply_text.len;
      table_result = amqp_encode_table(encoded, &(m->details), &offset);
      if (table_result < 0) return table_result;
      return offset;
    }
    case AMQP_CHANNEL_CLOSE_METHOD: {
      amqp_channel_close_t *m = (amqp_channel_close_t *) decoded;
      E_16(encoded, offset, m->reply_code);
      offset += 2;
      E_8(encoded, offset, m->reply_text.len);
      offset++;
      E_BYTES(encoded, offset, m->reply_text.len, m->reply_text.bytes);
      offset += m->reply_text.len;
      E_16(encoded, offset, m->class_id);
      offset += 2;
      E_16(encoded, offset, m->method_id);
      offset += 2;
      return offset;
    }
    case AMQP_CHANNEL_CLOSE_OK_METHOD: {
      return offset;
    }
    case AMQP_ACCESS_REQUEST_METHOD: {
      amqp_access_request_t *m = (amqp_access_request_t *) decoded;
      E_8(encoded, offset, m->realm.len);
      offset++;
      E_BYTES(encoded, offset, m->realm.len, m->realm.bytes);
      offset += m->realm.len;
      bit_buffer = 0;
      if (m->exclusive) { bit_buffer |= (1 << 0); }
      if (m->passive) { bit_buffer |= (1 << 1); }
      if (m->active) { bit_buffer |= (1 << 2); }
      if (m->write) { bit_buffer |= (1 << 3); }
      if (m->read) { bit_buffer |= (1 << 4); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_ACCESS_REQUEST_OK_METHOD: {
      amqp_access_request_ok_t *m = (amqp_access_request_ok_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      return offset;
    }
    case AMQP_EXCHANGE_DECLARE_METHOD: {
      amqp_exchange_declare_t *m = (amqp_exchange_declare_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->type.len);
      offset++;
      E_BYTES(encoded, offset, m->type.len, m->type.bytes);
      offset += m->type.len;
      bit_buffer = 0;
      if (m->passive) { bit_buffer |= (1 << 0); }
      if (m->durable) { bit_buffer |= (1 << 1); }
      if (m->auto_delete) { bit_buffer |= (1 << 2); }
      if (m->internal) { bit_buffer |= (1 << 3); }
      if (m->nowait) { bit_buffer |= (1 << 4); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      table_result = amqp_encode_table(encoded, &(m->arguments), &offset);
      if (table_result < 0) return table_result;
      return offset;
    }
    case AMQP_EXCHANGE_DECLARE_OK_METHOD: {
      return offset;
    }
    case AMQP_EXCHANGE_DELETE_METHOD: {
      amqp_exchange_delete_t *m = (amqp_exchange_delete_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      bit_buffer = 0;
      if (m->if_unused) { bit_buffer |= (1 << 0); }
      if (m->nowait) { bit_buffer |= (1 << 1); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_EXCHANGE_DELETE_OK_METHOD: {
      return offset;
    }
    case AMQP_QUEUE_DECLARE_METHOD: {
      amqp_queue_declare_t *m = (amqp_queue_declare_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      bit_buffer = 0;
      if (m->passive) { bit_buffer |= (1 << 0); }
      if (m->durable) { bit_buffer |= (1 << 1); }
      if (m->exclusive) { bit_buffer |= (1 << 2); }
      if (m->auto_delete) { bit_buffer |= (1 << 3); }
      if (m->nowait) { bit_buffer |= (1 << 4); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      table_result = amqp_encode_table(encoded, &(m->arguments), &offset);
      if (table_result < 0) return table_result;
      return offset;
    }
    case AMQP_QUEUE_DECLARE_OK_METHOD: {
      amqp_queue_declare_ok_t *m = (amqp_queue_declare_ok_t *) decoded;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      E_32(encoded, offset, m->message_count);
      offset += 4;
      E_32(encoded, offset, m->consumer_count);
      offset += 4;
      return offset;
    }
    case AMQP_QUEUE_BIND_METHOD: {
      amqp_queue_bind_t *m = (amqp_queue_bind_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      bit_buffer = 0;
      if (m->nowait) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      table_result = amqp_encode_table(encoded, &(m->arguments), &offset);
      if (table_result < 0) return table_result;
      return offset;
    }
    case AMQP_QUEUE_BIND_OK_METHOD: {
      return offset;
    }
    case AMQP_QUEUE_PURGE_METHOD: {
      amqp_queue_purge_t *m = (amqp_queue_purge_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      bit_buffer = 0;
      if (m->nowait) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_QUEUE_PURGE_OK_METHOD: {
      amqp_queue_purge_ok_t *m = (amqp_queue_purge_ok_t *) decoded;
      E_32(encoded, offset, m->message_count);
      offset += 4;
      return offset;
    }
    case AMQP_QUEUE_DELETE_METHOD: {
      amqp_queue_delete_t *m = (amqp_queue_delete_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      bit_buffer = 0;
      if (m->if_unused) { bit_buffer |= (1 << 0); }
      if (m->if_empty) { bit_buffer |= (1 << 1); }
      if (m->nowait) { bit_buffer |= (1 << 2); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_QUEUE_DELETE_OK_METHOD: {
      amqp_queue_delete_ok_t *m = (amqp_queue_delete_ok_t *) decoded;
      E_32(encoded, offset, m->message_count);
      offset += 4;
      return offset;
    }
    case AMQP_QUEUE_UNBIND_METHOD: {
      amqp_queue_unbind_t *m = (amqp_queue_unbind_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      table_result = amqp_encode_table(encoded, &(m->arguments), &offset);
      if (table_result < 0) return table_result;
      return offset;
    }
    case AMQP_QUEUE_UNBIND_OK_METHOD: {
      return offset;
    }
    case AMQP_BASIC_QOS_METHOD: {
      amqp_basic_qos_t *m = (amqp_basic_qos_t *) decoded;
      E_32(encoded, offset, m->prefetch_size);
      offset += 4;
      E_16(encoded, offset, m->prefetch_count);
      offset += 2;
      bit_buffer = 0;
      if (m->global) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_QOS_OK_METHOD: {
      return offset;
    }
    case AMQP_BASIC_CONSUME_METHOD: {
      amqp_basic_consume_t *m = (amqp_basic_consume_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      bit_buffer = 0;
      if (m->no_local) { bit_buffer |= (1 << 0); }
      if (m->no_ack) { bit_buffer |= (1 << 1); }
      if (m->exclusive) { bit_buffer |= (1 << 2); }
      if (m->nowait) { bit_buffer |= (1 << 3); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_CONSUME_OK_METHOD: {
      amqp_basic_consume_ok_t *m = (amqp_basic_consume_ok_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      return offset;
    }
    case AMQP_BASIC_CANCEL_METHOD: {
      amqp_basic_cancel_t *m = (amqp_basic_cancel_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      bit_buffer = 0;
      if (m->nowait) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_CANCEL_OK_METHOD: {
      amqp_basic_cancel_ok_t *m = (amqp_basic_cancel_ok_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      return offset;
    }
    case AMQP_BASIC_PUBLISH_METHOD: {
      amqp_basic_publish_t *m = (amqp_basic_publish_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      bit_buffer = 0;
      if (m->mandatory) { bit_buffer |= (1 << 0); }
      if (m->immediate) { bit_buffer |= (1 << 1); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_RETURN_METHOD: {
      amqp_basic_return_t *m = (amqp_basic_return_t *) decoded;
      E_16(encoded, offset, m->reply_code);
      offset += 2;
      E_8(encoded, offset, m->reply_text.len);
      offset++;
      E_BYTES(encoded, offset, m->reply_text.len, m->reply_text.bytes);
      offset += m->reply_text.len;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      return offset;
    }
    case AMQP_BASIC_DELIVER_METHOD: {
      amqp_basic_deliver_t *m = (amqp_basic_deliver_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->redelivered) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      return offset;
    }
    case AMQP_BASIC_GET_METHOD: {
      amqp_basic_get_t *m = (amqp_basic_get_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      bit_buffer = 0;
      if (m->no_ack) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_GET_OK_METHOD: {
      amqp_basic_get_ok_t *m = (amqp_basic_get_ok_t *) decoded;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->redelivered) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      E_32(encoded, offset, m->message_count);
      offset += 4;
      return offset;
    }
    case AMQP_BASIC_GET_EMPTY_METHOD: {
      amqp_basic_get_empty_t *m = (amqp_basic_get_empty_t *) decoded;
      E_8(encoded, offset, m->cluster_id.len);
      offset++;
      E_BYTES(encoded, offset, m->cluster_id.len, m->cluster_id.bytes);
      offset += m->cluster_id.len;
      return offset;
    }
    case AMQP_BASIC_ACK_METHOD: {
      amqp_basic_ack_t *m = (amqp_basic_ack_t *) decoded;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->multiple) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_REJECT_METHOD: {
      amqp_basic_reject_t *m = (amqp_basic_reject_t *) decoded;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->requeue) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_BASIC_RECOVER_METHOD: {
      amqp_basic_recover_t *m = (amqp_basic_recover_t *) decoded;
      bit_buffer = 0;
      if (m->requeue) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_FILE_QOS_METHOD: {
      amqp_file_qos_t *m = (amqp_file_qos_t *) decoded;
      E_32(encoded, offset, m->prefetch_size);
      offset += 4;
      E_16(encoded, offset, m->prefetch_count);
      offset += 2;
      bit_buffer = 0;
      if (m->global) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_FILE_QOS_OK_METHOD: {
      return offset;
    }
    case AMQP_FILE_CONSUME_METHOD: {
      amqp_file_consume_t *m = (amqp_file_consume_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      bit_buffer = 0;
      if (m->no_local) { bit_buffer |= (1 << 0); }
      if (m->no_ack) { bit_buffer |= (1 << 1); }
      if (m->exclusive) { bit_buffer |= (1 << 2); }
      if (m->nowait) { bit_buffer |= (1 << 3); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_FILE_CONSUME_OK_METHOD: {
      amqp_file_consume_ok_t *m = (amqp_file_consume_ok_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      return offset;
    }
    case AMQP_FILE_CANCEL_METHOD: {
      amqp_file_cancel_t *m = (amqp_file_cancel_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      bit_buffer = 0;
      if (m->nowait) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_FILE_CANCEL_OK_METHOD: {
      amqp_file_cancel_ok_t *m = (amqp_file_cancel_ok_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      return offset;
    }
    case AMQP_FILE_OPEN_METHOD: {
      amqp_file_open_t *m = (amqp_file_open_t *) decoded;
      E_8(encoded, offset, m->identifier.len);
      offset++;
      E_BYTES(encoded, offset, m->identifier.len, m->identifier.bytes);
      offset += m->identifier.len;
      E_64(encoded, offset, m->content_size);
      offset += 8;
      return offset;
    }
    case AMQP_FILE_OPEN_OK_METHOD: {
      amqp_file_open_ok_t *m = (amqp_file_open_ok_t *) decoded;
      E_64(encoded, offset, m->staged_size);
      offset += 8;
      return offset;
    }
    case AMQP_FILE_STAGE_METHOD: {
      return offset;
    }
    case AMQP_FILE_PUBLISH_METHOD: {
      amqp_file_publish_t *m = (amqp_file_publish_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      bit_buffer = 0;
      if (m->mandatory) { bit_buffer |= (1 << 0); }
      if (m->immediate) { bit_buffer |= (1 << 1); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      E_8(encoded, offset, m->identifier.len);
      offset++;
      E_BYTES(encoded, offset, m->identifier.len, m->identifier.bytes);
      offset += m->identifier.len;
      return offset;
    }
    case AMQP_FILE_RETURN_METHOD: {
      amqp_file_return_t *m = (amqp_file_return_t *) decoded;
      E_16(encoded, offset, m->reply_code);
      offset += 2;
      E_8(encoded, offset, m->reply_text.len);
      offset++;
      E_BYTES(encoded, offset, m->reply_text.len, m->reply_text.bytes);
      offset += m->reply_text.len;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      return offset;
    }
    case AMQP_FILE_DELIVER_METHOD: {
      amqp_file_deliver_t *m = (amqp_file_deliver_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->redelivered) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      E_8(encoded, offset, m->identifier.len);
      offset++;
      E_BYTES(encoded, offset, m->identifier.len, m->identifier.bytes);
      offset += m->identifier.len;
      return offset;
    }
    case AMQP_FILE_ACK_METHOD: {
      amqp_file_ack_t *m = (amqp_file_ack_t *) decoded;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->multiple) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_FILE_REJECT_METHOD: {
      amqp_file_reject_t *m = (amqp_file_reject_t *) decoded;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      bit_buffer = 0;
      if (m->requeue) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_STREAM_QOS_METHOD: {
      amqp_stream_qos_t *m = (amqp_stream_qos_t *) decoded;
      E_32(encoded, offset, m->prefetch_size);
      offset += 4;
      E_16(encoded, offset, m->prefetch_count);
      offset += 2;
      E_32(encoded, offset, m->consume_rate);
      offset += 4;
      bit_buffer = 0;
      if (m->global) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_STREAM_QOS_OK_METHOD: {
      return offset;
    }
    case AMQP_STREAM_CONSUME_METHOD: {
      amqp_stream_consume_t *m = (amqp_stream_consume_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      bit_buffer = 0;
      if (m->no_local) { bit_buffer |= (1 << 0); }
      if (m->exclusive) { bit_buffer |= (1 << 1); }
      if (m->nowait) { bit_buffer |= (1 << 2); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_STREAM_CONSUME_OK_METHOD: {
      amqp_stream_consume_ok_t *m = (amqp_stream_consume_ok_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      return offset;
    }
    case AMQP_STREAM_CANCEL_METHOD: {
      amqp_stream_cancel_t *m = (amqp_stream_cancel_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      bit_buffer = 0;
      if (m->nowait) { bit_buffer |= (1 << 0); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_STREAM_CANCEL_OK_METHOD: {
      amqp_stream_cancel_ok_t *m = (amqp_stream_cancel_ok_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      return offset;
    }
    case AMQP_STREAM_PUBLISH_METHOD: {
      amqp_stream_publish_t *m = (amqp_stream_publish_t *) decoded;
      E_16(encoded, offset, m->ticket);
      offset += 2;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      bit_buffer = 0;
      if (m->mandatory) { bit_buffer |= (1 << 0); }
      if (m->immediate) { bit_buffer |= (1 << 1); }
      E_8(encoded, offset, bit_buffer);
      offset++;
      return offset;
    }
    case AMQP_STREAM_RETURN_METHOD: {
      amqp_stream_return_t *m = (amqp_stream_return_t *) decoded;
      E_16(encoded, offset, m->reply_code);
      offset += 2;
      E_8(encoded, offset, m->reply_text.len);
      offset++;
      E_BYTES(encoded, offset, m->reply_text.len, m->reply_text.bytes);
      offset += m->reply_text.len;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->routing_key.len);
      offset++;
      E_BYTES(encoded, offset, m->routing_key.len, m->routing_key.bytes);
      offset += m->routing_key.len;
      return offset;
    }
    case AMQP_STREAM_DELIVER_METHOD: {
      amqp_stream_deliver_t *m = (amqp_stream_deliver_t *) decoded;
      E_8(encoded, offset, m->consumer_tag.len);
      offset++;
      E_BYTES(encoded, offset, m->consumer_tag.len, m->consumer_tag.bytes);
      offset += m->consumer_tag.len;
      E_64(encoded, offset, m->delivery_tag);
      offset += 8;
      E_8(encoded, offset, m->exchange.len);
      offset++;
      E_BYTES(encoded, offset, m->exchange.len, m->exchange.bytes);
      offset += m->exchange.len;
      E_8(encoded, offset, m->queue.len);
      offset++;
      E_BYTES(encoded, offset, m->queue.len, m->queue.bytes);
      offset += m->queue.len;
      return offset;
    }
    case AMQP_TX_SELECT_METHOD: {
      return offset;
    }
    case AMQP_TX_SELECT_OK_METHOD: {
      return offset;
    }
    case AMQP_TX_COMMIT_METHOD: {
      return offset;
    }
    case AMQP_TX_COMMIT_OK_METHOD: {
      return offset;
    }
    case AMQP_TX_ROLLBACK_METHOD: {
      return offset;
    }
    case AMQP_TX_ROLLBACK_OK_METHOD: {
      return offset;
    }
    case AMQP_DTX_SELECT_METHOD: {
      return offset;
    }
    case AMQP_DTX_SELECT_OK_METHOD: {
      return offset;
    }
    case AMQP_DTX_START_METHOD: {
      amqp_dtx_start_t *m = (amqp_dtx_start_t *) decoded;
      E_8(encoded, offset, m->dtx_identifier.len);
      offset++;
      E_BYTES(encoded, offset, m->dtx_identifier.len, m->dtx_identifier.bytes);
      offset += m->dtx_identifier.len;
      return offset;
    }
    case AMQP_DTX_START_OK_METHOD: {
      return offset;
    }
    case AMQP_TUNNEL_REQUEST_METHOD: {
      amqp_tunnel_request_t *m = (amqp_tunnel_request_t *) decoded;
      table_result = amqp_encode_table(encoded, &(m->meta_data), &offset);
      if (table_result < 0) return table_result;
      return offset;
    }
    case AMQP_TEST_INTEGER_METHOD: {
      amqp_test_integer_t *m = (amqp_test_integer_t *) decoded;
      E_8(encoded, offset, m->integer_1);
      offset++;
      E_16(encoded, offset, m->integer_2);
      offset += 2;
      E_32(encoded, offset, m->integer_3);
      offset += 4;
      E_64(encoded, offset, m->integer_4);
      offset += 8;
      E_8(encoded, offset, m->operation);
      offset++;
      return offset;
    }
    case AMQP_TEST_INTEGER_OK_METHOD: {
      amqp_test_integer_ok_t *m = (amqp_test_integer_ok_t *) decoded;
      E_64(encoded, offset, m->result);
      offset += 8;
      return offset;
    }
    case AMQP_TEST_STRING_METHOD: {
      amqp_test_string_t *m = (amqp_test_string_t *) decoded;
      E_8(encoded, offset, m->string_1.len);
      offset++;
      E_BYTES(encoded, offset, m->string_1.len, m->string_1.bytes);
      offset += m->string_1.len;
      E_32(encoded, offset, m->string_2.len);
      offset += 4;
      E_BYTES(encoded, offset, m->string_2.len, m->string_2.bytes);
      offset += m->string_2.len;
      E_8(encoded, offset, m->operation);
      offset++;
      return offset;
    }
    case AMQP_TEST_STRING_OK_METHOD: {
      amqp_test_string_ok_t *m = (amqp_test_string_ok_t *) decoded;
      E_32(encoded, offset, m->result.len);
      offset += 4;
      E_BYTES(encoded, offset, m->result.len, m->result.bytes);
      offset += m->result.len;
      return offset;
    }
    case AMQP_TEST_TABLE_METHOD: {
      amqp_test_table_t *m = (amqp_test_table_t *) decoded;
      table_result = amqp_encode_table(encoded, &(m->table), &offset);
      if (table_result < 0) return table_result;
      E_8(encoded, offset, m->integer_op);
      offset++;
      E_8(encoded, offset, m->string_op);
      offset++;
      return offset;
    }
    case AMQP_TEST_TABLE_OK_METHOD: {
      amqp_test_table_ok_t *m = (amqp_test_table_ok_t *) decoded;
      E_64(encoded, offset, m->integer_result);
      offset += 8;
      E_32(encoded, offset, m->string_result.len);
      offset += 4;
      E_BYTES(encoded, offset, m->string_result.len, m->string_result.bytes);
      offset += m->string_result.len;
      return offset;
    }
    case AMQP_TEST_CONTENT_METHOD: {
      return offset;
    }
    case AMQP_TEST_CONTENT_OK_METHOD: {
      amqp_test_content_ok_t *m = (amqp_test_content_ok_t *) decoded;
      E_32(encoded, offset, m->content_checksum);
      offset += 4;
      return offset;
    }
    default: return -ENOENT;
  }
}

int amqp_encode_properties(uint16_t class_id,
                           void *decoded,
                           amqp_bytes_t encoded)
{
  int offset = 0;
  int table_result;

  /* Cheat, and get the flags out generically, relying on the
     similarity of structure between classes */
  amqp_flags_t flags = * (amqp_flags_t *) decoded; /* cheating! */

  {
    /* We take a copy of flags to avoid destroying it, as it is used
       in the autogenerated code below. */
    amqp_flags_t remaining_flags = flags;
    do {
      amqp_flags_t remainder = remaining_flags >> 16;
      uint16_t partial_flags = remaining_flags & 0xFFFE;
      if (remainder != 0) { partial_flags |= 1; }
      E_16(encoded, offset, partial_flags);
      offset += 2;
      remaining_flags = remainder;
    } while (remaining_flags != 0);
  }
  
  switch (class_id) {
    case 10: {
      return offset;
    }
    case 20: {
      return offset;
    }
    case 30: {
      return offset;
    }
    case 40: {
      return offset;
    }
    case 50: {
      return offset;
    }
    case 60: {
      amqp_basic_properties_t *p = (amqp_basic_properties_t *) decoded;
      if (flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
        E_8(encoded, offset, p->content_type.len);
        offset++;
        E_BYTES(encoded, offset, p->content_type.len, p->content_type.bytes);
        offset += p->content_type.len;
      }
      if (flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
        E_8(encoded, offset, p->content_encoding.len);
        offset++;
        E_BYTES(encoded, offset, p->content_encoding.len, p->content_encoding.bytes);
        offset += p->content_encoding.len;
      }
      if (flags & AMQP_BASIC_HEADERS_FLAG) {
        table_result = amqp_encode_table(encoded, &(p->headers), &offset);
        if (table_result < 0) return table_result;
      }
      if (flags & AMQP_BASIC_DELIVERY_MODE_FLAG) {
        E_8(encoded, offset, p->delivery_mode);
        offset++;
      }
      if (flags & AMQP_BASIC_PRIORITY_FLAG) {
        E_8(encoded, offset, p->priority);
        offset++;
      }
      if (flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        E_8(encoded, offset, p->correlation_id.len);
        offset++;
        E_BYTES(encoded, offset, p->correlation_id.len, p->correlation_id.bytes);
        offset += p->correlation_id.len;
      }
      if (flags & AMQP_BASIC_REPLY_TO_FLAG) {
        E_8(encoded, offset, p->reply_to.len);
        offset++;
        E_BYTES(encoded, offset, p->reply_to.len, p->reply_to.bytes);
        offset += p->reply_to.len;
      }
      if (flags & AMQP_BASIC_EXPIRATION_FLAG) {
        E_8(encoded, offset, p->expiration.len);
        offset++;
        E_BYTES(encoded, offset, p->expiration.len, p->expiration.bytes);
        offset += p->expiration.len;
      }
      if (flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
        E_8(encoded, offset, p->message_id.len);
        offset++;
        E_BYTES(encoded, offset, p->message_id.len, p->message_id.bytes);
        offset += p->message_id.len;
      }
      if (flags & AMQP_BASIC_TIMESTAMP_FLAG) {
        E_64(encoded, offset, p->timestamp);
        offset += 8;
      }
      if (flags & AMQP_BASIC_TYPE_FLAG) {
        E_8(encoded, offset, p->type.len);
        offset++;
        E_BYTES(encoded, offset, p->type.len, p->type.bytes);
        offset += p->type.len;
      }
      if (flags & AMQP_BASIC_USER_ID_FLAG) {
        E_8(encoded, offset, p->user_id.len);
        offset++;
        E_BYTES(encoded, offset, p->user_id.len, p->user_id.bytes);
        offset += p->user_id.len;
      }
      if (flags & AMQP_BASIC_APP_ID_FLAG) {
        E_8(encoded, offset, p->app_id.len);
        offset++;
        E_BYTES(encoded, offset, p->app_id.len, p->app_id.bytes);
        offset += p->app_id.len;
      }
      if (flags & AMQP_BASIC_CLUSTER_ID_FLAG) {
        E_8(encoded, offset, p->cluster_id.len);
        offset++;
        E_BYTES(encoded, offset, p->cluster_id.len, p->cluster_id.bytes);
        offset += p->cluster_id.len;
      }
      return offset;
    }
    case 70: {
      amqp_file_properties_t *p = (amqp_file_properties_t *) decoded;
      if (flags & AMQP_FILE_CONTENT_TYPE_FLAG) {
        E_8(encoded, offset, p->content_type.len);
        offset++;
        E_BYTES(encoded, offset, p->content_type.len, p->content_type.bytes);
        offset += p->content_type.len;
      }
      if (flags & AMQP_FILE_CONTENT_ENCODING_FLAG) {
        E_8(encoded, offset, p->content_encoding.len);
        offset++;
        E_BYTES(encoded, offset, p->content_encoding.len, p->content_encoding.bytes);
        offset += p->content_encoding.len;
      }
      if (flags & AMQP_FILE_HEADERS_FLAG) {
        table_result = amqp_encode_table(encoded, &(p->headers), &offset);
        if (table_result < 0) return table_result;
      }
      if (flags & AMQP_FILE_PRIORITY_FLAG) {
        E_8(encoded, offset, p->priority);
        offset++;
      }
      if (flags & AMQP_FILE_REPLY_TO_FLAG) {
        E_8(encoded, offset, p->reply_to.len);
        offset++;
        E_BYTES(encoded, offset, p->reply_to.len, p->reply_to.bytes);
        offset += p->reply_to.len;
      }
      if (flags & AMQP_FILE_MESSAGE_ID_FLAG) {
        E_8(encoded, offset, p->message_id.len);
        offset++;
        E_BYTES(encoded, offset, p->message_id.len, p->message_id.bytes);
        offset += p->message_id.len;
      }
      if (flags & AMQP_FILE_FILENAME_FLAG) {
        E_8(encoded, offset, p->filename.len);
        offset++;
        E_BYTES(encoded, offset, p->filename.len, p->filename.bytes);
        offset += p->filename.len;
      }
      if (flags & AMQP_FILE_TIMESTAMP_FLAG) {
        E_64(encoded, offset, p->timestamp);
        offset += 8;
      }
      if (flags & AMQP_FILE_CLUSTER_ID_FLAG) {
        E_8(encoded, offset, p->cluster_id.len);
        offset++;
        E_BYTES(encoded, offset, p->cluster_id.len, p->cluster_id.bytes);
        offset += p->cluster_id.len;
      }
      return offset;
    }
    case 80: {
      amqp_stream_properties_t *p = (amqp_stream_properties_t *) decoded;
      if (flags & AMQP_STREAM_CONTENT_TYPE_FLAG) {
        E_8(encoded, offset, p->content_type.len);
        offset++;
        E_BYTES(encoded, offset, p->content_type.len, p->content_type.bytes);
        offset += p->content_type.len;
      }
      if (flags & AMQP_STREAM_CONTENT_ENCODING_FLAG) {
        E_8(encoded, offset, p->content_encoding.len);
        offset++;
        E_BYTES(encoded, offset, p->content_encoding.len, p->content_encoding.bytes);
        offset += p->content_encoding.len;
      }
      if (flags & AMQP_STREAM_HEADERS_FLAG) {
        table_result = amqp_encode_table(encoded, &(p->headers), &offset);
        if (table_result < 0) return table_result;
      }
      if (flags & AMQP_STREAM_PRIORITY_FLAG) {
        E_8(encoded, offset, p->priority);
        offset++;
      }
      if (flags & AMQP_STREAM_TIMESTAMP_FLAG) {
        E_64(encoded, offset, p->timestamp);
        offset += 8;
      }
      return offset;
    }
    case 90: {
      return offset;
    }
    case 100: {
      return offset;
    }
    case 110: {
      amqp_tunnel_properties_t *p = (amqp_tunnel_properties_t *) decoded;
      if (flags & AMQP_TUNNEL_HEADERS_FLAG) {
        table_result = amqp_encode_table(encoded, &(p->headers), &offset);
        if (table_result < 0) return table_result;
      }
      if (flags & AMQP_TUNNEL_PROXY_NAME_FLAG) {
        E_8(encoded, offset, p->proxy_name.len);
        offset++;
        E_BYTES(encoded, offset, p->proxy_name.len, p->proxy_name.bytes);
        offset += p->proxy_name.len;
      }
      if (flags & AMQP_TUNNEL_DATA_NAME_FLAG) {
        E_8(encoded, offset, p->data_name.len);
        offset++;
        E_BYTES(encoded, offset, p->data_name.len, p->data_name.bytes);
        offset += p->data_name.len;
      }
      if (flags & AMQP_TUNNEL_DURABLE_FLAG) {
        E_8(encoded, offset, p->durable);
        offset++;
      }
      if (flags & AMQP_TUNNEL_BROADCAST_FLAG) {
        E_8(encoded, offset, p->broadcast);
        offset++;
      }
      return offset;
    }
    case 120: {
      return offset;
    }
    default: return -ENOENT;
  }
}
