#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>

#include "cmeshcore.h"
#include "cmeshcore-utils.h"
#include "cmeshcore-buffer.h"


typedef struct cmeshcore {
  int32_t fd;
  cmeshcore_buffer rxbuf;
  cmeshcore_buffer txbuf;
} cmeshcore;


typedef enum cmeshcore_frame {
  cmeshcore_frame_in  = 0x3e,
  cmeshcore_frame_out = 0x3c,
} cmeshcore_frame;

typedef enum cmeshcore_cmd {
  cmeshcore_cmd_app_start         = 1,
  cmeshcore_cmd_send_txt_msg      = 2,
  cmeshcore_cmd_send_chan_txt_msg = 3,
  cmeshcore_cmd_send_self_advert  = 7,
  cmeshcore_cmd_device_query      = 22,
} cmeshcore_cmd;

typedef enum cmeshcore_resp {
  cmeshcore_resp_ok          = 0,
  cmeshcore_resp_error       = 1,
  cmeshcore_resp_self_info   = 5,
  cmeshcore_resp_sent        = 6,
  cmeshcore_resp_device_info = 13,
} cmeshcore_resp;

typedef enum cmeshcore_error {
  cmeshcore_error_unsupported_cmd = 1,
  cmeshcore_error_not_found       = 2,
  cmeshcore_error_table_full      = 3,
  cmeshcore_error_bad_state       = 4,
  cmeshcore_error_file_io         = 5,
  cmeshcore_error_illegal_arg     = 6,
} cmeshcore_error;

typedef enum cmeshcore_txt_type {
  cmeshcore_txt_type_plain        = 0,
  cmeshcore_txt_type_cli_data     = 1,
  cmeshcore_txt_type_signed_plain = 2,
} cmeshcore_txt_type;

typedef enum cmeshcore_app_version {
  cmeshcore_app_version_1 = 1,
  cmeshcore_app_version_2 = 2,
  cmeshcore_app_version_3 = 3,
} cmeshcore_app_version;

typedef enum cmeshcore_self_advert_type {
  cmeshcore_self_advert_type_zero_hop = 0,
  cmeshcore_self_advert_type_flood    = 1,
} cmeshcore_self_advert_type;


static int32_t cmeshcore_write(
    int32_t fd,
    const uint8_t *bytes,
    uint32_t n)
{
  CMESHCORE_ASSERT(fd >= 0);

  if (n == 0) { return 0; }

  CMESHCORE_ASSERT(bytes);

  uint32_t written = 0;

  while (written < n) {
    int32_t rv = write(fd, bytes + written, n - written);

    if (rv == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      } else {
        break;
      }
    } else if (rv == 0) {
      break;
    } else {
      written += rv;
    }
  }

  return written == n ? 0 : -1;
}

static int32_t cmeshcore_write_frame(
    int32_t fd,
    cmeshcore_buffer *txbuf)
{
  CMESHCORE_ASSERT(fd >= 0);
  CMESHCORE_ASSERT(txbuf);

  if (cmeshcore_write(fd, txbuf->data, txbuf->size) == -1) {
    return -1;
  }

  if (tcdrain(fd) == -1) {
    return -1;
  }

  return 0;
}

static int32_t cmeshcore_write_app_start(cmeshcore *mesh) {
  CMESHCORE_ASSERT(mesh);

  const uint8_t payload[] = {
    cmeshcore_cmd_app_start,
    cmeshcore_app_version_3,
    0, 0, 0, 0, 0, 0, // reserved
    0x63, 0x6d, 0x65, 0x73, 0x68, 0x63, 0x6f, 0x72, 0x65 // cmeshcore
  };

  const uint8_t frame_header[3] = {
    cmeshcore_frame_out,
    sizeof(payload) & 0xFF,
    (sizeof(payload) >> 8) & 0xFF,
  };

  cmeshcore_buffer_clear(&mesh->txbuf);

  if (cmeshcore_buffer_append(&mesh->txbuf, frame_header, sizeof(frame_header)) != 0) {
    return -1;
  }

  if (cmeshcore_buffer_append(&mesh->txbuf, payload, sizeof(payload)) != 0) {
    return -1;
  }

  return cmeshcore_write_frame(mesh->fd, &mesh->txbuf);
}

static int32_t cmeshcore_poll(int32_t fd, int32_t timeout) {
  CMESHCORE_ASSERT(fd >= 0);
  CMESHCORE_ASSERT(timeout >= 0);

  struct pollfd pfd = {
    .fd = fd,
    .events = POLLIN,
    .revents = 0,
  };

  int32_t rv = poll(&pfd, 1, timeout);

  if (rv == -1) {
    if (errno == EINTR) {
      return 0;
    } else {
      return -1;
    }
  }

  if (rv == 0) {
    return 0;
  }

  if (pfd.revents & POLLIN) {
    return 1;
  }

  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    return -1;
  }

  CMESHCORE_ASSERT(0);
  return -1;
}

static int32_t cmeshcore_read_bytes(
    int32_t fd,
    cmeshcore_buffer *rxbuf,
    size_t max_bytes)
{
  CMESHCORE_ASSERT(fd >= 0);
  CMESHCORE_ASSERT(rxbuf);

  uint8_t temp[1024];

  const size_t to_read = max_bytes < sizeof(temp) ? max_bytes : sizeof(temp);

  int32_t n = read(fd, temp, to_read);

  if (n == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    } else if (errno == EINTR) {
      return 0;
    } else {
      return -1;
    }
  }

  if (n == 0) {
    return -1;
  }

  if (cmeshcore_buffer_append(rxbuf, temp, n) != 0) {
    return -1;
  }

  return n;
}

static int32_t cmeshcore_extract_frame(
    cmeshcore_buffer *rxbuf,
    uint8_t **out_payload,
    size_t *out_size)
{
  CMESHCORE_ASSERT(rxbuf);
  CMESHCORE_ASSERT(out_payload);
  CMESHCORE_ASSERT(out_size);

  if (rxbuf->size < 3) {
    return -1;
  }

  if (rxbuf->data[0] != cmeshcore_frame_in) {
    cmeshcore_buffer_consume(rxbuf, 1);
    return -2;
  }

  const uint16_t payload_size = rxbuf->data[1] | (rxbuf->data[2] << 8);

  if (payload_size == 0) {
    cmeshcore_buffer_consume(rxbuf, 1);
    return -2;
  }

  const size_t frame_size = 3 + payload_size;

  if (rxbuf->size < frame_size) {
    return -1;
  }

  *out_payload = rxbuf->data + 3;
  *out_size = payload_size;

  return 0;
}

static int32_t cmeshcore_check_packet_match(
    const uint8_t packet_type,
    const uint8_t *expected_types,
    size_t num_expected)
{
  for (size_t i = 0; i < num_expected; i++) {
    if (packet_type == expected_types[i]) {
      return 1;
    }
  }

  return 0;
}

static int32_t cmeshcore_wait_for_packet_types(
    cmeshcore *mesh,
    const uint8_t *expected_types,
    size_t num_expected,
    uint8_t **out_payload_ptr,
    size_t *out_payload_size,
    int32_t timeout_ms)
{
  CMESHCORE_ASSERT(mesh);
  CMESHCORE_ASSERT(expected_types);
  CMESHCORE_ASSERT(num_expected > 0);

  const int64_t deadline_ms = (int64_t)timeout_ms + (int64_t)time(NULL) * 1000;

  while (1) {
    uint8_t *payload;
    size_t payload_size;

    int32_t rv = cmeshcore_extract_frame(&mesh->rxbuf, &payload, &payload_size);

    if (rv == 0) {
      const size_t frame_size = 3 + payload_size;

      if (payload_size >= 1) {
        const uint8_t packet_type = payload[0];

        if (cmeshcore_check_packet_match(packet_type, expected_types, num_expected)) {
          if (out_payload_ptr && payload_size > 1) {
            *out_payload_ptr = payload + 1;

            if (out_payload_size) {
              *out_payload_size = payload_size - 1;
            }
          } else {
            if (out_payload_ptr) {
              *out_payload_ptr = NULL;
            }

            if (out_payload_size) {
              *out_payload_size = 0;
            }
          }

          cmeshcore_buffer_consume(&mesh->rxbuf, frame_size);

          return packet_type;
        }
      }

      cmeshcore_buffer_consume(&mesh->rxbuf, frame_size);

      continue;
    }

    if (rv == -2) {
      continue;
    }

    const int64_t now_ms = (int64_t)time(NULL) * 1000;
    const int32_t remaining_ms = (int32_t)(deadline_ms - now_ms);

    if (remaining_ms <= 0) {
      return -1;
    }

    rv = cmeshcore_poll(mesh->fd, remaining_ms);

    if (rv <= 0) {
      return -1;
    }

    rv = cmeshcore_read_bytes(mesh->fd, &mesh->rxbuf, 1024);

    if (rv < 0) {
      return -1;
    }
  }
}

static int32_t cmeshcore_wait_for_self_info(cmeshcore *mesh, int32_t timeout_ms) {
  CMESHCORE_ASSERT(mesh);
  CMESHCORE_ASSERT(timeout_ms > 0);

  const uint8_t expected[] = { cmeshcore_resp_self_info };

  // TODO: parse response frame

  int32_t packet_type = cmeshcore_wait_for_packet_types(
      mesh, expected, 1, NULL, NULL, timeout_ms);

  return packet_type == cmeshcore_resp_self_info ? 0 : -1;
}


cmeshcore* cmeshcore_new(const char *port) {
  if (!port) { return NULL; }

  cmeshcore *mesh = malloc(sizeof(cmeshcore));
  if (!mesh) { return NULL; }

  cmeshcore_buffer rxbuf;

  if (cmeshcore_buffer_init(&rxbuf, 4096) != 0) {
    free(mesh);
    return NULL;
  }

  cmeshcore_buffer txbuf;

  if (cmeshcore_buffer_init(&txbuf, 4096) != 0) {
    cmeshcore_buffer_free(&rxbuf);
    free(mesh);
    return NULL;
  }

  int32_t fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd == -1) {
    cmeshcore_buffer_free(&txbuf);
    cmeshcore_buffer_free(&rxbuf);
    free(mesh);
    return NULL;
  }

  struct termios opts;

  if (tcgetattr(fd, &opts) == -1) {
    close(fd);
    cmeshcore_buffer_free(&txbuf);
    cmeshcore_buffer_free(&rxbuf);
    free(mesh);
    return NULL;
  }

  opts.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK);
  opts.c_iflag &= ~(IXON | IXOFF | IXANY);
  opts.c_oflag &= ~(OPOST);
  opts.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG);
  opts.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
  opts.c_cflag |= (CLOCAL | CREAD | CS8);

  cfsetispeed(&opts, B115200);
  cfsetospeed(&opts, B115200);

  if (tcsetattr(fd, TCSAFLUSH, &opts) == -1) {
    close(fd);
    cmeshcore_buffer_free(&txbuf);
    cmeshcore_buffer_free(&rxbuf);
    free(mesh);
    return NULL;
  }

  // Initialize mesh struct early so we can use txbuf
  *mesh = (cmeshcore) {
    .fd = fd,
    .rxbuf = rxbuf,
    .txbuf = txbuf,
  };

  int32_t rv = cmeshcore_write_app_start(mesh);

  if (rv == -1) {
    tcflush(fd, TCIOFLUSH);
    close(fd);
    cmeshcore_buffer_free(&mesh->txbuf);
    cmeshcore_buffer_free(&mesh->rxbuf);
    free(mesh);
    return NULL;
  }

  rv = cmeshcore_wait_for_self_info(mesh, 10000);

  if (rv == -1) {
    tcflush(fd, TCIOFLUSH);
    close(fd);
    cmeshcore_buffer_free(&mesh->txbuf);
    cmeshcore_buffer_free(&mesh->rxbuf);
    free(mesh);
    return NULL;
  }

  return mesh;
}


void cmeshcore_free(cmeshcore * const mesh) {
  if (!mesh) { return; }

  if (mesh->fd != -1) {
    tcflush(mesh->fd, TCIOFLUSH);
    close(mesh->fd);
    mesh->fd = -1;
  }

  cmeshcore_buffer_free(&mesh->txbuf);
  cmeshcore_buffer_free(&mesh->rxbuf);

  free(mesh);
}

int32_t cmeshcore_send_msg_txt(
    cmeshcore *mesh,
    const uint8_t pk[6],
    const char *msg)
{
  CMESHCORE_ASSERT(mesh);
  CMESHCORE_ASSERT(pk);

  if (!msg) { return -1; }

  const uint32_t msg_len = strlen(msg);
  const uint32_t ts = (uint32_t)time(NULL);

  const uint32_t payload_len = 7 + 6 + msg_len;

  const uint8_t frame_header[3] = {
    cmeshcore_frame_out,
    payload_len & 0xFF,
    (payload_len >> 8) & 0xFF,
  };

  cmeshcore_buffer_clear(&mesh->txbuf);

  if (cmeshcore_buffer_append(&mesh->txbuf, frame_header, sizeof(frame_header)) != 0) {
    return -1;
  }

  const uint8_t payload_header[] = {
    cmeshcore_cmd_send_txt_msg,
    cmeshcore_txt_type_plain,
    0,
    ts & 0xFF,
    (ts >> 8) & 0xFF,
    (ts >> 16) & 0xFF,
    (ts >> 24) & 0xFF,
  };

  if (cmeshcore_buffer_append(&mesh->txbuf, payload_header, sizeof(payload_header)) != 0) {
    return -1;
  }

  if (cmeshcore_buffer_append(&mesh->txbuf, pk, 6) != 0) {
    return -1;
  }

  if (cmeshcore_buffer_append(&mesh->txbuf, (const uint8_t*)msg, msg_len) != 0) {
    return -1;
  }

  if (cmeshcore_write_frame(mesh->fd, &mesh->txbuf) != 0) {
    return -1;
  }

  const uint8_t expected[] = { cmeshcore_resp_sent, cmeshcore_resp_error };

  // TODO: parse response frame

  int32_t packet_type = cmeshcore_wait_for_packet_types(
      mesh, expected, 2, NULL, NULL, 5000);

  return packet_type == cmeshcore_resp_sent ? 0 : -1;
}

int32_t cmeshcore_advert_self_flood(cmeshcore_s mesh) {
  CMESHCORE_ASSERT(mesh);

  const uint8_t payload[2] = {
    cmeshcore_cmd_send_self_advert,
    cmeshcore_self_advert_type_flood,
  };

  const uint8_t frame_header[3] = {
    cmeshcore_frame_out,
    sizeof(payload) & 0xFF,
    (sizeof(payload) >> 8) & 0xFF,
  };

  cmeshcore_buffer_clear(&mesh->txbuf);

  if (cmeshcore_buffer_append(&mesh->txbuf, frame_header, sizeof(frame_header)) != 0) {
    return -1;
  }

  if (cmeshcore_buffer_append(&mesh->txbuf, payload, sizeof(payload)) != 0) {
    return -1;
  }

  if (cmeshcore_write_frame(mesh->fd, &mesh->txbuf) != 0) {
    return -1;
  }

  const uint8_t expected[] = { cmeshcore_resp_ok, cmeshcore_resp_error };

  // TODO: parse response frame

  int32_t packet_type = cmeshcore_wait_for_packet_types(
      mesh, expected, 2, NULL, NULL, 5000);

  return packet_type == cmeshcore_resp_ok ? 0 : -1;
}

int32_t cmeshcore_advert_self_zero_hop(cmeshcore_s mesh) {
  CMESHCORE_ASSERT(mesh);

  const uint8_t payload[2] = {
    cmeshcore_cmd_send_self_advert,
    cmeshcore_self_advert_type_zero_hop,
  };

  const uint8_t frame_header[3] = {
    cmeshcore_frame_out,
    sizeof(payload) & 0xFF,
    (sizeof(payload) >> 8) & 0xFF,
  };

  cmeshcore_buffer_clear(&mesh->txbuf);

  if (cmeshcore_buffer_append(&mesh->txbuf, frame_header, sizeof(frame_header)) != 0) {
    return -1;
  }

  if (cmeshcore_buffer_append(&mesh->txbuf, payload, sizeof(payload)) != 0) {
    return -1;
  }

  if (cmeshcore_write_frame(mesh->fd, &mesh->txbuf) != 0) {
    return -1;
  }

  const uint8_t expected[] = { cmeshcore_resp_ok, cmeshcore_resp_error };

  // TODO: parse response frame

  int32_t packet_type = cmeshcore_wait_for_packet_types(
      mesh, expected, 2, NULL, NULL, 5000);

  return packet_type == cmeshcore_resp_ok ? 0 : -1;
}
