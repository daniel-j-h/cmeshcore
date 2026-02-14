#ifndef CMESHCORE_BUFFER_H
#define CMESHCORE_BUFFER_H

#include <stdint.h>

#include "cmeshcore-utils.h"


typedef struct cmeshcore_buffer {
  uint8_t *data;
  size_t size;
  size_t capacity;
} cmeshcore_buffer;


CMESHCORE_WARN_UNUSED
int32_t cmeshcore_buffer_init(
    cmeshcore_buffer *buf,
    size_t capacity);

void cmeshcore_buffer_free(
    cmeshcore_buffer *buf);

CMESHCORE_WARN_UNUSED
int32_t cmeshcore_buffer_grow(
    cmeshcore_buffer *buf,
    size_t capacity);

CMESHCORE_WARN_UNUSED
int32_t cmeshcore_buffer_append(
    cmeshcore_buffer *buf,
    const uint8_t *bytes,
    size_t bytes_len);

void cmeshcore_buffer_clear(
    cmeshcore_buffer *buf);

void cmeshcore_buffer_consume(
    cmeshcore_buffer *buf,
    size_t bytes_len);


#endif
