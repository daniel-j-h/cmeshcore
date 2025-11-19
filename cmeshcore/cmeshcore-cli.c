#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "cmeshcore.h"


int main(int argc, char *argv[]) {
  const char *port = argc == 2 ? argv[1] : "/dev/ttyACM0";

  cmeshcore_s mesh = cmeshcore_new(port);

  if (!mesh) {
    fprintf(stderr, "error: unable to construct meshcore\n");
    return EXIT_FAILURE;
  }

  const char *msg = "Hi!";
  const uint8_t pk[6] = {0x40, 0x2b, 0xfe, 0x9d, 0x13, 0x2e};

  int32_t rv = cmeshcore_send_msg_txt(mesh, pk, msg);

  if (rv == -1) {
    fprintf(stderr, "error: unable to send message\n");
    cmeshcore_free(mesh);
    return EXIT_FAILURE;
  }

  cmeshcore_free(mesh);
  return EXIT_SUCCESS;
}
