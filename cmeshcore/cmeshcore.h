#ifndef CMESHCORE_H
#define CMESHCORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if __GNUC__ >= 4
  #define CMESHCORE_API __attribute__((visibility("default")))
  #define CMESHCORE_WARN_UNUSED __attribute__((warn_unused_result))
#else
  #define CMESHCORE_API
  #define CMESHCORE_WARN_UNUSED
#endif


/**
* The device handle
*/
typedef struct cmeshcore* cmeshcore_s;
typedef const struct cmeshcore* cmeshcore_const_s;


/**
 * Creates a new device handle.
 *
 * The caller is responsible to destruct the
 * returned device handle with `cmeshcore_free`
 */
CMESHCORE_API
CMESHCORE_WARN_UNUSED
cmeshcore_s cmeshcore_new(const char *port);

/**
 * Destructs a device handle.
 *
 * The caller is responsible to call this
 * on device handles from `cmeshcore_new`
 */
CMESHCORE_API
void cmeshcore_free(cmeshcore_s);

/**
 * Sends text message on the device handle.
 *
 * The device must know about the contact
 * identified by the public key prefix.
 */
CMESHCORE_API
CMESHCORE_WARN_UNUSED
int32_t cmeshcore_send_msg_txt(
    cmeshcore_s mesh,
    const uint8_t pk[6],
    const char *msg);

/**
 * Sends a flood advert on the device handle.
 */
CMESHCORE_API
CMESHCORE_WARN_UNUSED
int32_t cmeshcore_advert_self_flood(cmeshcore_s mesh);

/**
 * Sends a zero-hop advert on the device handle.
 */
CMESHCORE_API
CMESHCORE_WARN_UNUSED
int32_t cmeshcore_advert_self_zero_hop(cmeshcore_s mesh);


#ifdef __cplusplus
}
#endif

#endif
