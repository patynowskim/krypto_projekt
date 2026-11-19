#include "sdls_lite.h"

#include <string.h>

void sdls_pack_iv(uint8_t iv[SAT_IV_LEN], uint32_t epoch, uint64_t counter)
{
    (void)epoch;
    (void)counter;
    if (iv) {
        memset(iv, 0, SAT_IV_LEN);
    }
}

void sdls_unpack_iv(const uint8_t iv[SAT_IV_LEN], uint32_t *epoch, uint64_t *counter)
{
    (void)iv;
    if (epoch) {
        *epoch = 0;
    }
    if (counter) {
        *counter = 0;
    }
}

void sdls_pack_spi(uint8_t out[SAT_SPI_LEN], uint16_t spi)
{
    out[0] = (uint8_t)(spi >> 8);
    out[1] = (uint8_t)spi;
}

uint16_t sdls_unpack_spi(const uint8_t in[SAT_SPI_LEN])
{
    return (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
}

void sdls_sa_init(sdls_sa_t *sa, const uint8_t key[SAT_KEY_LEN], uint32_t epoch)
{
    if (!sa) {
        return;
    }
    memset(sa, 0, sizeof(*sa));
    if (key) {
        memcpy(sa->key, key, SAT_KEY_LEN);
        sa->has_key = true;
    }
    sa->tx_epoch = epoch;
}

int sdls_next_iv(sdls_sa_t *sa, uint8_t iv[SAT_IV_LEN])
{
    if (!sa || !iv || !sa->has_key) {
        return SAT_ERR_ARG;
    }
    /* TODO: pack epoch + counter */
    memset(iv, 0, SAT_IV_LEN);
    sa->tx_next++;
    return SAT_OK;
}

int sdls_encrypt(const sdls_sa_t *sa,
                 const uint8_t iv[SAT_IV_LEN],
                 const uint8_t *aad, size_t aad_len,
                 const uint8_t *pt, size_t pt_len,
                 uint8_t *ct,
                 uint8_t tag[SAT_TAG_LEN])
{
    (void)sa;
    (void)iv;
    (void)aad;
    (void)aad_len;
    (void)pt;
    (void)pt_len;
    (void)ct;
    (void)tag;
    /* TODO: mbedtls AES-256-GCM */
    return SAT_ERR_CRYPTO;
}

int sdls_decrypt(const sdls_sa_t *sa,
                 const uint8_t iv[SAT_IV_LEN],
                 const uint8_t *aad, size_t aad_len,
                 const uint8_t *ct, size_t ct_len,
                 const uint8_t tag[SAT_TAG_LEN],
                 uint8_t *pt)
{
    (void)sa;
    (void)iv;
    (void)aad;
    (void)aad_len;
    (void)ct;
    (void)ct_len;
    (void)tag;
    (void)pt;
    /* TODO: mbedtls AES-256-GCM */
    return SAT_ERR_CRYPTO;
}

bool sdls_replay_too_old(const sdls_sa_t *sa, const uint8_t iv[SAT_IV_LEN])
{
    (void)sa;
    (void)iv;
    /* TODO: anti-replay window check */
    return false;
}

int sdls_replay_commit(sdls_sa_t *sa, const uint8_t iv[SAT_IV_LEN])
{
    (void)sa;
    (void)iv;
    /* TODO: anti-replay bitmap update */
    return SAT_ERR_CRYPTO;
}

int sdls_self_test(void)
{
    /* TODO: NIST AES-256-GCM test vector */
    return SAT_ERR_CRYPTO;
}
