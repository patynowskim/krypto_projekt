#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sat_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  key[SAT_KEY_LEN];
    uint32_t tx_epoch;
    uint64_t tx_next;          /* next counter to emit; never reused with this epoch */
    uint32_t rx_epoch;
    uint64_t rx_highest;
    uint64_t rx_bitmap;        /* bit 0 = rx_highest, bit k = rx_highest-k */
    bool     rx_inited;
    bool     has_key;
} sdls_sa_t;

void sdls_sa_init(sdls_sa_t *sa, const uint8_t key[SAT_KEY_LEN], uint32_t epoch);

void sdls_pack_iv(uint8_t iv[SAT_IV_LEN], uint32_t epoch, uint64_t counter);
void sdls_unpack_iv(const uint8_t iv[SAT_IV_LEN], uint32_t *epoch, uint64_t *counter);
void sdls_pack_spi(uint8_t out[SAT_SPI_LEN], uint16_t spi);
uint16_t sdls_unpack_spi(const uint8_t in[SAT_SPI_LEN]);

/* Fill iv with (tx_epoch || tx_next) and increment tx_next. */
int sdls_next_iv(sdls_sa_t *sa, uint8_t iv[SAT_IV_LEN]);

int sdls_encrypt(const sdls_sa_t *sa,
                 const uint8_t iv[SAT_IV_LEN],
                 const uint8_t *aad, size_t aad_len,
                 const uint8_t *pt, size_t pt_len,
                 uint8_t *ct,
                 uint8_t tag[SAT_TAG_LEN]);

int sdls_decrypt(const sdls_sa_t *sa,
                 const uint8_t iv[SAT_IV_LEN],
                 const uint8_t *aad, size_t aad_len,
                 const uint8_t *ct, size_t ct_len,
                 const uint8_t tag[SAT_TAG_LEN],
                 uint8_t *pt);

/*
 * Call ONLY after successful authentication.
 * Returns SAT_ERR_REPLAY if the IV is a duplicate or too old.
 */
int sdls_replay_commit(sdls_sa_t *sa, const uint8_t iv[SAT_IV_LEN]);

/* Cheap pre-filter: too-old IVs can be dropped before GCM. */
bool sdls_replay_too_old(const sdls_sa_t *sa, const uint8_t iv[SAT_IV_LEN]);

int sdls_self_test(void);

#ifdef __cplusplus
}
#endif
