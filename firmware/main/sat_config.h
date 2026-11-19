#pragma once

#include <stdint.h>

/* ESP-NOW v2 payload limit (ESP_NOW_MAX_DATA_LEN_V2, ESP-IDF >= 5.4). */
#define SAT_ESPNOW_MAX          1470u

#define SAT_SPP_PRIMARY_LEN     6u
#define SAT_SPP_VERSION         0u
#define SAT_SPP_TYPE_TM         0u
#define SAT_SPP_TYPE_TC         1u
#define SAT_SPP_SEQ_UNSEGMENTED 3u
#define SAT_SPP_SEQ_MOD         16384u

#define SAT_APID_TM_HK          0x001u
#define SAT_APID_TM_PING        0x002u
#define SAT_APID_TC             0x010u
#define SAT_APID_IDLE           0x7FFu

/* SDLS Annex E baseline mapped onto the ESP-NOW payload. */
#define SAT_SPI_LEN             2u
#define SAT_IV_LEN              12u
#define SAT_TAG_LEN             16u
#define SAT_SDLS_HDR_LEN        (SAT_SPI_LEN + SAT_IV_LEN)          /* 14 */
#define SAT_SDLS_OVERHEAD       (SAT_SDLS_HDR_LEN + SAT_TAG_LEN)    /* 30 */
#define SAT_CRYPTO_OVERHEAD     (SAT_SPP_PRIMARY_LEN + SAT_SDLS_OVERHEAD) /* 36 */
#define SAT_USER_MAX            (SAT_ESPNOW_MAX - SAT_CRYPTO_OVERHEAD)    /* 1434 */

#define SAT_SPI_DEFAULT         0x0001u
#define SAT_KEY_LEN             32u
#define SAT_REPLAY_WINDOW       64u

#define SAT_UART_BAUD           115200
#define SAT_LED_GPIO            2

typedef enum {
    SAT_OK            =  0,
    SAT_ERR_ARG       = -1,
    SAT_ERR_LEN       = -2,
    SAT_ERR_AUTH      = -3,
    SAT_ERR_REPLAY    = -4,
    SAT_ERR_SPI       = -5,
    SAT_ERR_CRYPTO    = -6,
    SAT_ERR_DROPPED   = -7,
    SAT_ERR_BUSY      = -8,
} sat_err_t;
