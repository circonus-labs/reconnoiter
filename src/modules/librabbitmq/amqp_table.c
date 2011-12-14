#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "amqp.h"
#include "amqp_private.h"

#include <assert.h>

#define INITIAL_TABLE_SIZE 16

int amqp_decode_table(amqp_bytes_t encoded,
		      amqp_pool_t *pool,
		      amqp_table_t *output,
		      int *offsetptr)
{
  int offset = *offsetptr;
  uint32_t tablesize = D_32(encoded, offset);
  int num_entries = 0;
  amqp_table_entry_t *entries = malloc(INITIAL_TABLE_SIZE * sizeof(amqp_table_entry_t));
  int allocated_entries = INITIAL_TABLE_SIZE;
  int limit;

  if (entries == NULL) {
    return -ENOMEM;
  }

  offset += 4;
  limit = offset + tablesize;

  while (offset < limit) {
    size_t keylen;
    amqp_table_entry_t *entry;

    keylen = D_8(encoded, offset);
    offset++;

    if (num_entries >= allocated_entries) {
      void *newentries;
      allocated_entries = allocated_entries * 2;
      newentries = realloc(entries, allocated_entries * sizeof(amqp_table_entry_t));
      if (newentries == NULL) {
	free(entries);
	return -ENOMEM;
      }
      entries = newentries;
    }
    entry = &entries[num_entries];

    entry->key.len = keylen;
    entry->key.bytes = D_BYTES(encoded, offset, keylen);
    offset += keylen;

    entry->kind = D_8(encoded, offset);
    offset++;

    switch (entry->kind) {
      case 'S':
	entry->value.bytes.len = D_32(encoded, offset);
	offset += 4;
	entry->value.bytes.bytes = D_BYTES(encoded, offset, entry->value.bytes.len);
	offset += entry->value.bytes.len;
	break;
      case 'I':
	entry->value.i32 = (int32_t) D_32(encoded, offset);
	offset += 4;
	break;
      case 'D':
	entry->value.decimal.decimals = D_8(encoded, offset);
	offset++;
	entry->value.decimal.value = D_32(encoded, offset);
	offset += 4;
	break;
      case 'T':
	entry->value.u64 = D_64(encoded, offset);
	offset += 8;
	break;
      case 'F':
	(void)AMQP_CHECK_RESULT(amqp_decode_table(encoded, pool, &(entry->value.table), &offset));
	break;
      default:
	free(entries);
	return -EINVAL;
    }

    num_entries++;
  }

  output->num_entries = num_entries;
  output->entries = amqp_pool_alloc(pool, num_entries * sizeof(amqp_table_entry_t));
  memcpy(output->entries, entries, num_entries * sizeof(amqp_table_entry_t));
  free(entries);

  *offsetptr = offset;
  return 0;
}

int amqp_encode_table(amqp_bytes_t encoded,
		      amqp_table_t *input,
		      int *offsetptr)
{
  int offset = *offsetptr;
  int tablesize_offset = offset;
  int i;

  offset += 4; /* skip space for the size of the table to be filled in later */

  for (i = 0; i < input->num_entries; i++) {
    amqp_table_entry_t *entry = &(input->entries[i]);

    E_8(encoded, offset, entry->key.len);
    offset++;

    E_BYTES(encoded, offset, entry->key.len, entry->key.bytes);
    offset += entry->key.len;

    E_8(encoded, offset, entry->kind);
    offset++;

    switch (entry->kind) {
      case 'S':
	E_32(encoded, offset, entry->value.bytes.len);
	offset += 4;
	E_BYTES(encoded, offset, entry->value.bytes.len, entry->value.bytes.bytes);
	offset += entry->value.bytes.len;
	break;
      case 'I':
	E_32(encoded, offset, (uint32_t) entry->value.i32);
	offset += 4;
	break;
      case 'D':
	E_8(encoded, offset, entry->value.decimal.decimals);
	offset++;
	E_32(encoded, offset, entry->value.decimal.value);
	offset += 4;
	break;
      case 'T':
	E_64(encoded, offset, entry->value.u64);
	offset += 8;
	break;
      case 'F':
	(void)AMQP_CHECK_RESULT(amqp_encode_table(encoded, &(entry->value.table), &offset));
	break;
      default:
	return -EINVAL;
    }
  }

  E_32(encoded, tablesize_offset, (offset - *offsetptr - 4));
  *offsetptr = offset;
  return 0;
}

int amqp_table_entry_cmp(void const *entry1, void const *entry2) {
  amqp_table_entry_t const *p1 = (amqp_table_entry_t const *) entry1;
  amqp_table_entry_t const *p2 = (amqp_table_entry_t const *) entry2;

  int d;
  int minlen;

  minlen = p1->key.len;
  if (p2->key.len < minlen) minlen = p2->key.len;

  d = memcmp(p1->key.bytes, p2->key.bytes, minlen);
  if (d != 0) {
    return d;
  }

  return p1->key.len - p2->key.len;
}
