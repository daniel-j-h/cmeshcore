#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "cmeshcore.h"

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>

#ifndef CMESHCORE_ASSERT
#ifndef NDEBUG
#define CMESHCORE_ASSERT(x) (assert(x))
#else
#define CMESHCORE_ASSERT(x)
#endif
#else
#endif


typedef struct cmeshcore {
  int32_t fd;
} cmeshcore;


typedef enum cmeshcore_frame {
  cmeshcore_frame_in  = 0x3e,
  cmeshcore_frame_out = 0x3c,
} cmeshcore_frame;

typedef enum cmeshcore_cmd {
  cmeshcore_cmd_app_start         = 1,
  cmeshcore_cmd_send_txt_msg      = 2,
  cmeshcore_cmd_send_chan_txt_msg = 3,
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

typedef enum cmeshcore_advert_type {
  cmeshcore_advert_type_none     = 0,
  cmeshcore_advert_type_chat     = 1,
  cmeshcore_advert_type_repeater = 2,
  cmeshcore_advert_type_room     = 3,
} cmeshcore_advert_type;


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
    const uint8_t *payload,
    uint32_t payload_len)
{
  CMESHCORE_ASSERT(fd >= 0);
  CMESHCORE_ASSERT(payload_len <= UINT16_MAX);

  const uint8_t header[3] = {
    cmeshcore_frame_out,
    payload_len & 0xFF,
    (payload_len >> 8) & 0xFF,
  };

  if (cmeshcore_write(fd, header, sizeof(header)) == -1) {
    return -1;
  }

  if (cmeshcore_write(fd, payload, payload_len) == -1) {
    return -1;
  }

  if (tcdrain(fd) == -1) {
    return -1;
  }

  return 0;
}

static int32_t cmeshcore_write_app_start(int32_t fd) {
  CMESHCORE_ASSERT(fd >= 0);

  const uint8_t payload[] = {
    cmeshcore_cmd_app_start,
    cmeshcore_app_version_3,
    0, 0, 0, 0, 0, 0, // reserved
    0x63, 0x6d, 0x65, 0x73, 0x68, 0x63, 0x6f, 0x72, 0x65 // cmeshcore
  };

  return cmeshcore_write_frame(fd, payload, sizeof(payload));
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

static int32_t cmeshcore_wait_for_self_info(int32_t fd, int32_t timeout) {
  CMESHCORE_ASSERT(fd >= 0);
  CMESHCORE_ASSERT(timeout > 0);

  // TODO: make this robust, meaning
  // - handle EINTR, EAGAIN
  // - proper read buffer
  // - partial reads
  // - pushed frames before self info
  // - parse full self info frame

  int32_t rv = cmeshcore_poll(fd, timeout);

  if (rv <= 0) {
    return -1;
  }

  uint8_t buf[4096];

  const uint32_t n = read(fd, buf, sizeof(buf));

  if (n < 4) {
    return -1;
  }

  if (buf[0] != cmeshcore_frame_in) {
    return -1;
  }

  if (buf[3] != cmeshcore_resp_self_info) {
    return -1;
  }

  return 0;
}


cmeshcore* cmeshcore_new(const char *port) {
  if (!port) { return NULL; }

  cmeshcore *mesh = malloc(sizeof(cmeshcore));
  if (!mesh) { return NULL; }

  int32_t fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd == -1) {
    free(mesh);
    return NULL;
  }

  struct termios opts;

  if (tcgetattr(fd, &opts) == -1) {
    close(fd);
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
    free(mesh);
    return NULL;
  }

  *mesh = (cmeshcore) {
    .fd = fd,
  };

  int32_t rv = cmeshcore_write_app_start(fd);

  if (rv == -1) {
    close(fd);
    free(mesh);
    return NULL;
  }

  rv = cmeshcore_wait_for_self_info(fd, 2000);

  if (rv == -1) {
    close(fd);
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

  // cmd(1) + type(1) + attempt(1) + timestamp(4) + pk(6) + msg
  const uint32_t payload_len = 7 + 6 + msg_len;

  // TODO: arbitrary limit so we do not need
  // to take care of dynamic memory management

  if (payload_len > 4096) {
    return -1;
  }

  uint8_t payload[4096];

  const uint32_t ts = (uint32_t)time(NULL);

  payload[0] = cmeshcore_cmd_send_txt_msg;
  payload[1] = cmeshcore_txt_type_plain;
  payload[2] = 0; // attempt
  payload[3] = ts & 0xFF;
  payload[4] = (ts >> 8) & 0xFF;
  payload[5] = (ts >> 16) & 0xFF;
  payload[6] = (ts >> 24) & 0xFF;

  memcpy(payload + 7, pk, 6);
  memcpy(payload + 13, msg, msg_len);

  return cmeshcore_write_frame(mesh->fd, payload, payload_len);

  // TODO: wait for device response for status
}
