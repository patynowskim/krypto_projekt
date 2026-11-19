#include "ccsds_spp.h"

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

int spp_pack_primary(uint8_t out[SAT_SPP_PRIMARY_LEN], const spp_primary_t *h)
{
    if (!out || !h) {
        return SAT_ERR_ARG;
    }
    if (!spp_primary_valid(h)) {
        return SAT_ERR_ARG;
    }

    /* bits 0-2 version, 3 type, 4 SH flag, 5-15 APID */
    uint16_t id = (uint16_t)(((uint16_t)(h->version & 0x7u) << 13) |
                             ((uint16_t)(h->type & 0x1u) << 12) |
                             ((uint16_t)(h->sec_hdr & 0x1u) << 11) |
                             (h->apid & 0x7FFu));

    /* bits 16-17 sequence flags, 18-31 sequence count */
    uint16_t seq = (uint16_t)(((uint16_t)(h->seq_flags & 0x3u) << 14) |
                              (h->seq_count & 0x3FFFu));

    put_be16(out, id);
    put_be16(out + 2, seq);
    put_be16(out + 4, h->data_len);
    return SAT_OK;
}

int spp_unpack_primary(const uint8_t in[SAT_SPP_PRIMARY_LEN], spp_primary_t *h)
{
    if (!in || !h) {
        return SAT_ERR_ARG;
    }

    uint16_t id  = get_be16(in);
    uint16_t seq = get_be16(in + 2);

    h->version   = (uint8_t)((id >> 13) & 0x7u);
    h->type      = (uint8_t)((id >> 12) & 0x1u);
    h->sec_hdr   = (uint8_t)((id >> 11) & 0x1u);
    h->apid      = (uint16_t)(id & 0x7FFu);
    h->seq_flags = (uint8_t)((seq >> 14) & 0x3u);
    h->seq_count = (uint16_t)(seq & 0x3FFFu);
    h->data_len  = get_be16(in + 4);
    return SAT_OK;
}

bool spp_primary_valid(const spp_primary_t *h)
{
    if (!h) {
        return false;
    }
    if (h->version != SAT_SPP_VERSION) {
        return false;
    }
    if (h->type > 1u || h->sec_hdr > 1u) {
        return false;
    }
    if (h->apid > 0x7FFu) {
        return false;
    }
    if (h->seq_flags > 3u || h->seq_count > 0x3FFFu) {
        return false;
    }
    if (h->apid == SAT_APID_IDLE && h->sec_hdr != 0u) {
        return false;
    }
    return true;
}

uint16_t spp_encode_data_length(size_t data_field_octets)
{
    if (data_field_octets == 0u || data_field_octets > 65536u) {
        return 0xFFFFu; /* caller must treat as invalid */
    }
    return (uint16_t)(data_field_octets - 1u);
}

size_t spp_decode_data_length(uint16_t field)
{
    return (size_t)field + 1u;
}
