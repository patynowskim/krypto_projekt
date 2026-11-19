#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sat_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  version;     /* 3 bits, SPP = 0 */
    uint8_t  type;        /* 0 = TM, 1 = TC */
    uint8_t  sec_hdr;     /* 1 if Packet Secondary Header present */
    uint16_t apid;        /* 11 bits */
    uint8_t  seq_flags;   /* 2 bits, 3 = unsegmented */
    uint16_t seq_count;   /* 14 bits, modulo 16384, per APID */
    uint16_t data_len;    /* Packet Data Length = (data field octets) - 1 */
} spp_primary_t;

int spp_pack_primary(uint8_t out[SAT_SPP_PRIMARY_LEN], const spp_primary_t *h);
int spp_unpack_primary(const uint8_t in[SAT_SPP_PRIMARY_LEN], spp_primary_t *h);
bool spp_primary_valid(const spp_primary_t *h);

/* data_field_octets must be 1..65536; stored as N-1. */
uint16_t spp_encode_data_length(size_t data_field_octets);
size_t   spp_decode_data_length(uint16_t field);

#ifdef __cplusplus
}
#endif
