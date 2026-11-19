#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ccsds_spp.h"
#include "sdls_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wire layout of one ESP-NOW payload (offsets):
 *   0     6   CCSDS Packet Primary Header          (AAD, clear)
 *   6     2   SPI                                  (AAD, clear)
 *   8    12   IV = epoch || counter                (AAD, clear)
 *  20     N   ciphertext(User Data Field)
 *  20+N  16   AES-GCM tag
 *
 * Packet Data Length counts N (plaintext user octets), not SPI/IV/tag.
 */

int sat_frame_seal(sdls_sa_t *sa,
                   uint8_t type,
                   uint16_t apid,
                   uint16_t seq_count,
                   const uint8_t *user,
                   size_t user_len,
                   uint8_t *out,
                   size_t *out_len);

int sat_frame_open(sdls_sa_t *sa,
                   const uint8_t *in,
                   size_t in_len,
                   spp_primary_t *hdr,
                   uint8_t *user,
                   size_t *user_len);

int sat_frame_self_test(void);

#ifdef __cplusplus
}
#endif
