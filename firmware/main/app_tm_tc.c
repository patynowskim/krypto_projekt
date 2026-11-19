#include "app_tm_tc.h"

#include <string.h>

#include "channel_emu.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "metrics.h"
#include "sat_frame.h"

static const char *TAG = "tm_tc";

static sdls_sa_t *s_out;
static sdls_sa_t *s_in;
static sat_role_t s_role;
static uint32_t s_interval_ms;
static uint16_t s_seq[8];
static SemaphoreHandle_t s_mu;

static uint8_t s_last_tx[SAT_ESPNOW_MAX];
static uint8_t s_tx_buf[SAT_ESPNOW_MAX];
static size_t s_last_tx_len;

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint16_t next_seq(uint16_t apid)
{
    unsigned idx = 0;
    switch (apid) {
    case SAT_APID_TM_HK:   idx = 0; break;
    case SAT_APID_TM_PING: idx = 1; break;
    case SAT_APID_TC:      idx = 2; break;
    default:               idx = 3; break;
    }
    uint16_t s = s_seq[idx];
    s_seq[idx] = (uint16_t)((s + 1u) % SAT_SPP_SEQ_MOD);
    return s;
}

static const char *err_name(int rc)
{
    switch (rc) {
    case SAT_OK:          return "OK";
    case SAT_ERR_AUTH:    return "AUTH_FAIL";
    case SAT_ERR_REPLAY:  return "REPLAY";
    case SAT_ERR_SPI:     return "BAD_SPI";
    case SAT_ERR_LEN:     return "BAD_LEN";
    case SAT_ERR_DROPPED: return "EMU_DROP";
    default:              return "ERR";
    }
}

int app_tm_tc_send(uint8_t type, uint16_t apid, const uint8_t *user, size_t user_len)
{
    size_t frame_len = 0;
    uint32_t epoch = 0;
    uint64_t iv_ctr = 0;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    uint16_t seq = next_seq(apid);
    int64_t t0 = esp_timer_get_time();
    int rc = sat_frame_seal(s_out, type, apid, seq, user, user_len, s_tx_buf, &frame_len);
    uint32_t enc_us = (uint32_t)(esp_timer_get_time() - t0);
    if (rc == SAT_OK) {
        memcpy(s_last_tx, s_tx_buf, frame_len);
        s_last_tx_len = frame_len;
        sdls_unpack_iv(s_tx_buf + SAT_SPP_PRIMARY_LEN + SAT_SPI_LEN, &epoch, &iv_ctr);
        rc = espnow_link_send(s_tx_buf, frame_len);
    }
    xSemaphoreGive(s_mu);

    if (rc == SAT_OK) {
        metrics_tx_ok(enc_us);
        metrics_log("TX", apid, seq, iv_ctr, (int)frame_len, "OK", 0, enc_us);
    } else if (rc != SAT_ERR_DROPPED) {
        metrics_tx_fail();
        metrics_log("TX", apid, seq, iv_ctr, (int)user_len, err_name(rc), 0, enc_us);
    }
    return rc;
}

int app_tm_tc_send_hk(void)
{
    metrics_t m;
    metrics_get(&m);
    uint8_t buf[26];
    put_be32(buf, (uint32_t)(esp_timer_get_time() / 1000));
    buf[4] = (uint8_t)m.last_rssi;
    buf[5] = (uint8_t)gpio_get_level(SAT_LED_GPIO);
    put_be32(buf + 6, (uint32_t)esp_get_free_heap_size());
    put_be32(buf + 10, m.tx_ok);
    put_be32(buf + 14, m.rx_ok);
    put_be32(buf + 18, m.rx_auth_fail);
    put_be32(buf + 22, m.rx_replay);
    return app_tm_tc_send(SAT_SPP_TYPE_TM, SAT_APID_TM_HK, buf, sizeof(buf));
}

int app_tm_tc_send_ping(void)
{
    uint8_t buf[5];
    buf[0] = TC_CMD_PING;
    put_be32(buf + 1, (uint32_t)(esp_timer_get_time() / 1000));
    return app_tm_tc_send(SAT_SPP_TYPE_TC, SAT_APID_TC, buf, sizeof(buf));
}

int app_tm_tc_send_led(uint8_t on)
{
    uint8_t buf[2] = {TC_CMD_LED, on ? 1u : 0u};
    return app_tm_tc_send(SAT_SPP_TYPE_TC, SAT_APID_TC, buf, sizeof(buf));
}

int app_tm_tc_replay_last(void)
{
    int rc;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_last_tx_len == 0u) {
        rc = SAT_ERR_BUSY;
    } else {
        ESP_LOGW(TAG, "replaying last TX (%u B) — peer should drop REPLAY",
                 (unsigned)s_last_tx_len);
        rc = espnow_link_send(s_last_tx, s_last_tx_len);
    }
    xSemaphoreGive(s_mu);
    return rc;
}

static void handle_tc(const uint8_t *user, size_t len)
{
    if (len == 0u) {
        return;
    }
    switch (user[0]) {
    case TC_CMD_LED:
        if (len >= 2u) {
            gpio_set_level(SAT_LED_GPIO, user[1] ? 1 : 0);
            ESP_LOGI(TAG, "LED %s", user[1] ? "on" : "off");
        }
        break;
    case TC_CMD_INTERVAL:
        if (len >= 5u) {
            app_tm_tc_set_interval(get_be32(user + 1));
        }
        break;
    case TC_CMD_EMU:
        if (len >= 11u) {
            channel_cfg_t c;
            c.delay_ms = get_be32(user + 1);
            c.drop_permille = (uint16_t)((user[5] << 8) | user[6]);
            c.ber_e6 = get_be32(user + 7);
            channel_emu_set(&c);
            ESP_LOGI(TAG, "emu delay=%lu drop=%u/1000 ber_e6=%lu",
                     (unsigned long)c.delay_ms, (unsigned)c.drop_permille,
                     (unsigned long)c.ber_e6);
        }
        break;
    case TC_CMD_PING:
        /* Echo as TM ping so RTT can be measured at GCS. */
        app_tm_tc_send(SAT_SPP_TYPE_TM, SAT_APID_TM_PING, user, len);
        break;
    default:
        ESP_LOGW(TAG, "unknown TC 0x%02X", user[0]);
        break;
    }
}

void app_tm_tc_on_rx(const uint8_t *frame, size_t len, int8_t rssi)
{
    spp_primary_t hdr;
    uint8_t user[SAT_USER_MAX];
    size_t user_len = 0;
    uint32_t epoch = 0;
    uint64_t iv_ctr = 0;

    if (len >= SAT_SPP_PRIMARY_LEN + SAT_SDLS_HDR_LEN) {
        sdls_unpack_iv(frame + SAT_SPP_PRIMARY_LEN + SAT_SPI_LEN, &epoch, &iv_ctr);
    }

    int64_t t0 = esp_timer_get_time();
    int rc = sat_frame_open(s_in, frame, len, &hdr, user, &user_len);
    uint32_t dec_us = (uint32_t)(esp_timer_get_time() - t0);

    metrics_rx(rc, rssi, dec_us);
    metrics_log("RX",
                rc == SAT_OK ? hdr.apid : 0,
                rc == SAT_OK ? hdr.seq_count : 0,
                iv_ctr, (int)len, err_name(rc), rssi, dec_us);

    if (rc != SAT_OK) {
        ESP_LOGW(TAG, "open %s (%d) rssi=%d", err_name(rc), rc, rssi);
        return;
    }

    ESP_LOGI(TAG, "RX type=%u apid=0x%03X seq=%u n=%u rssi=%d",
             hdr.type, hdr.apid, hdr.seq_count, (unsigned)user_len, rssi);

    if (hdr.apid == SAT_APID_TC && hdr.type == SAT_SPP_TYPE_TC) {
        handle_tc(user, user_len);
    } else if (hdr.apid == SAT_APID_TM_HK && user_len >= 26u) {
        ESP_LOGI(TAG, "HK uptime=%lu heap=%lu tx=%lu rx=%lu auth_fail=%lu replay=%lu",
                 (unsigned long)get_be32(user),
                 (unsigned long)get_be32(user + 6),
                 (unsigned long)get_be32(user + 10),
                 (unsigned long)get_be32(user + 14),
                 (unsigned long)get_be32(user + 18),
                 (unsigned long)get_be32(user + 22));
    } else if (hdr.apid == SAT_APID_TM_PING && user_len >= 5u) {
        uint32_t sent_ms = get_be32(user + 1);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "PONG rtt~%lu ms (unsynced clocks)",
                 (unsigned long)(now_ms - sent_ms));
    }
}

static void tm_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t iv = s_interval_ms;
        if (iv == 0u) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (s_role == SAT_ROLE_CUBESAT) {
            app_tm_tc_send_hk();
        }
        vTaskDelay(pdMS_TO_TICKS(iv));
    }
}

void app_tm_tc_init(sdls_sa_t *sa_out, sdls_sa_t *sa_in, sat_role_t role)
{
    s_out = sa_out;
    s_in = sa_in;
    s_role = role;
    s_mu = xSemaphoreCreateMutex();

    gpio_reset_pin(SAT_LED_GPIO);
    gpio_set_direction(SAT_LED_GPIO, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(SAT_LED_GPIO, 0);

#ifdef CONFIG_SATLINK_TM_INTERVAL_MS
    s_interval_ms = CONFIG_SATLINK_TM_INTERVAL_MS;
#else
    s_interval_ms = (role == SAT_ROLE_CUBESAT) ? 1000u : 0u;
#endif
    if (role == SAT_ROLE_GCS) {
        s_interval_ms = 0;
    }

    xTaskCreate(tm_task, "tm", 4096, NULL, 5, NULL);
}

void app_tm_tc_set_role(sat_role_t role)
{
    if (role != s_role && s_out && s_in) {
        sdls_sa_t *tmp = s_out;
        s_out = s_in;
        s_in = tmp;
    }
    s_role = role;
    if (role == SAT_ROLE_GCS) {
        s_interval_ms = 0;
    } else if (s_interval_ms == 0u) {
        s_interval_ms = 1000;
    }
    ESP_LOGI(TAG, "role %s (TM/TC keys swapped if role changed)",
             role == SAT_ROLE_CUBESAT ? "cubesat" : "gcs");
}

sat_role_t app_tm_tc_role(void)
{
    return s_role;
}

void app_tm_tc_set_interval(uint32_t interval_ms)
{
    s_interval_ms = interval_ms;
    ESP_LOGI(TAG, "TM interval %lu ms", (unsigned long)interval_ms);
}
