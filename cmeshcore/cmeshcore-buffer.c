#include <stdlib.h>
#include <string.h>

#include "cmeshcore-buffer.h"


int32_t cmeshcore_buffer_init(
    cmeshcore_buffer *buf,
    size_t capacity)
{
  CMESHCORE_ASSERT(buf);

  buf->data = malloc(capacity);

  if (!buf->data) {
    buf->size = 0;
    buf->capacity = 0;

    return -1;
  }

  buf->size = 0;
  buf->capacity = capacity;

  return 0;
}

void cmeshcore_buffer_free(cmeshcore_buffer *buf) {
  if (!buf) { return; }

  free(buf->data);

  buf->data = NULL;
  buf->size = 0;
  buf->capacity = 0;
}

int32_t cmeshcore_buffer_grow(
    cmeshcore_buffer *buf,
    size_t capacity)
{
  CMESHCORE_ASSERT(buf);

  if (capacity <= buf->capacity) { return 0; }

  size_t new_capacity = buf->capacity ? buf->capacity : 64;

  while (new_capacity < capacity) {
    const size_t next = new_capacity + new_capacity / 2;

    if (next <= new_capacity) {
      new_capacity = capacity;
      break;
    }

    new_capacity = next;
  }

  uint8_t *new_data = realloc(buf->data, new_capacity);
  if (!new_data) { return -1; }

  buf->data = new_data;
  buf->capacity = new_capacity;

  return 0;
}

int32_t cmeshcore_buffer_append(
  cmeshcore_buffer *buf,
  const uint8_t *bytes,
  size_t bytes_len)
{
  CMESHCORE_ASSERT(buf);

  if (bytes_len == 0) { return 0; }

  CMESHCORE_ASSERT(bytes);

  if (cmeshcore_buffer_grow(buf, buf->size + bytes_len) != 0) {
    return -1;
  }

  memcpy(buf->data + buf->size, bytes, bytes_len);
  buf->size += bytes_len;

  return 0;
}

void cmeshcore_buffer_clear(cmeshcore_buffer *buf) {
  CMESHCORE_ASSERT(buf);

  buf->size = 0;
}

void cmeshcore_buffer_consume(cmeshcore_buffer *buf, size_t bytes_len) {
  CMESHCORE_ASSERT(buf);

  if (bytes_len == 0) { return; }

  if (bytes_len >= buf->size) {
    buf->size = 0;

    return;
  }

  memmove(buf->data, buf->data + bytes_len, buf->size - bytes_len);

  buf->size -= bytes_len;
}
