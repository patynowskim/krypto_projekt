#include "sat_frame.h"

#include <string.h>

int sat_frame_seal(sdls_sa_t *sa,
                   uint8_t type,
                   uint16_t apid,
                   uint16_t seq_count,
                   const uint8_t *user,
                   size_t user_len,
                   uint8_t *out,
                   size_t *out_len)
{
    if (!sa || !out || !out_len || (user_len && !user)) {
        return SAT_ERR_ARG;
    }
    if (user_len == 0u || user_len > SAT_USER_MAX) {
        return SAT_ERR_LEN;
    }

    const size_t framed = SAT_CRYPTO_OVERHEAD + user_len;
    if (framed > SAT_ESPNOW_MAX) {
        return SAT_ERR_LEN;
    }

    spp_primary_t h = {
        .version   = SAT_SPP_VERSION,
        .type      = type,
        .sec_hdr   = 0,
        .apid      = apid,
        .seq_flags = SAT_SPP_SEQ_UNSEGMENTED,
        .seq_count = (uint16_t)(seq_count & 0x3FFFu),
        .data_len  = spp_encode_data_length(user_len),
    };

    uint8_t *primary = out;
    uint8_t *spi     = out + SAT_SPP_PRIMARY_LEN;
    uint8_t *iv      = spi + SAT_SPI_LEN;
    uint8_t *ct      = iv + SAT_IV_LEN;
    uint8_t *tag     = ct + user_len;

    int rc = spp_pack_primary(primary, &h);
    if (rc != SAT_OK) {
        return rc;
    }
    sdls_pack_spi(spi, SAT_SPI_DEFAULT);
    rc = sdls_next_iv(sa, iv);
    if (rc != SAT_OK) {
        return rc;
    }

    /* AAD = Primary Header || SPI || IV */
    rc = sdls_encrypt(sa, iv, out, SAT_SPP_PRIMARY_LEN + SAT_SDLS_HDR_LEN,
                      user, user_len, ct, tag);
    if (rc != SAT_OK) {
        return rc;
    }

    *out_len = framed;
    return SAT_OK;
}

int sat_frame_open(sdls_sa_t *sa,
                   const uint8_t *in,
                   size_t in_len,
                   spp_primary_t *hdr,
                   uint8_t *user,
                   size_t *user_len)
{
    if (!sa || !in || !hdr || !user || !user_len) {
        return SAT_ERR_ARG;
    }
    if (in_len < SAT_CRYPTO_OVERHEAD + 1u || in_len > SAT_ESPNOW_MAX) {
        return SAT_ERR_LEN;
    }

    const size_t ct_len = in_len - SAT_CRYPTO_OVERHEAD;
    const uint8_t *primary = in;
    const uint8_t *spi_b   = in + SAT_SPP_PRIMARY_LEN;
    const uint8_t *iv      = spi_b + SAT_SPI_LEN;
    const uint8_t *ct      = iv + SAT_IV_LEN;
    const uint8_t *tag     = ct + ct_len;

    int rc = spp_unpack_primary(primary, hdr);
    if (rc != SAT_OK || !spp_primary_valid(hdr)) {
        return SAT_ERR_ARG;
    }
    if (spp_decode_data_length(hdr->data_len) != ct_len) {
        return SAT_ERR_LEN;
    }

    uint16_t spi = sdls_unpack_spi(spi_b);
    if (spi != SAT_SPI_DEFAULT) {
        return SAT_ERR_SPI;
    }

    if (sdls_replay_too_old(sa, iv)) {
        return SAT_ERR_REPLAY;
    }

    rc = sdls_decrypt(sa, iv,
                      in, SAT_SPP_PRIMARY_LEN + SAT_SDLS_HDR_LEN,
                      ct, ct_len, tag, user);
    if (rc != SAT_OK) {
        return rc;
    }

    rc = sdls_replay_commit(sa, iv);
    if (rc != SAT_OK) {
        memset(user, 0, ct_len);
        return rc;
    }

    *user_len = ct_len;
    return SAT_OK;
}

int sat_frame_self_test(void)
{
    /* Matches host/test_protocol.py GOLDEN_PING_FRAME. */
    static const uint8_t key[SAT_KEY_LEN] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    };
    static const uint8_t expect[] = {
        0x10, 0x10, 0xc0, 0x01, 0x00, 0x03, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x14, 0x7f, 0x83, 0xed,
        0xe9, 0xa1, 0xa8, 0x17, 0x66, 0xbb, 0x58, 0x70,
        0x01, 0x73, 0x39, 0x7f, 0x34, 0xeb, 0x13, 0xf2
    };
    static const uint8_t ping[] = {'P', 'I', 'N', 'G'};

    sdls_sa_t tx;
    sdls_sa_t rx;
    sdls_sa_init(&tx, key, 1);
    sdls_sa_init(&rx, key, 1);

    uint8_t frame[64];
    size_t frame_len = 0;
    int rc = sat_frame_seal(&tx, SAT_SPP_TYPE_TC, SAT_APID_TC, 1,
                            ping, sizeof(ping), frame, &frame_len);
    if (rc != SAT_OK || frame_len != sizeof(expect) ||
        memcmp(frame, expect, sizeof(expect)) != 0) {
        return SAT_ERR_CRYPTO;
    }

    spp_primary_t hdr;
    uint8_t user[16];
    size_t user_len = 0;
    rc = sat_frame_open(&rx, frame, frame_len, &hdr, user, &user_len);
    if (rc != SAT_OK || user_len != sizeof(ping) ||
        memcmp(user, ping, sizeof(ping)) != 0) {
        return SAT_ERR_AUTH;
    }
    if (sat_frame_open(&rx, frame, frame_len, &hdr, user, &user_len) != SAT_ERR_REPLAY) {
        return SAT_ERR_REPLAY;
    }
    return SAT_OK;
}
