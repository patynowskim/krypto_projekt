#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_tm_tc.h"
#include "channel_emu.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "metrics.h"
#include "sat_config.h"

static const char *TAG = "cli";

static void print_help(void)
{
    printf(
        "commands:\n"
        "  help\n"
        "  mac\n"
        "  peer aa:bb:cc:dd:ee:ff\n"
        "  role cubesat|gcs\n"
        "  hk                 send housekeeping TM\n"
        "  ping               TC ping (CubeSat echoes TM)\n"
        "  led on|off         TC to peer (from GCS) or local\n"
        "  interval <ms>      CubeSat TM period, 0=off\n"
        "  emu delay <ms>\n"
        "  emu drop <permille>   0..1000\n"
        "  emu ber <e6>          100 => BER 1e-4\n"
        "  emu show\n"
        "  replay             re-send last sealed frame (anti-replay demo)\n"
        "  stats\n"
    );
}

static void print_mac(const char *label, const uint8_t mac[6])
{
    printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n",
           label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int parse_mac(const char *s, uint8_t mac[6])
{
    unsigned b[6];
    if (sscanf(s, " %02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)b[i];
    }
    return 0;
}

static void handle_line(char *line)
{
    char *nl = strchr(line, '\n');
    if (nl) {
        *nl = '\0';
    }
    char *cr = strchr(line, '\r');
    if (cr) {
        *cr = '\0';
    }
    if (line[0] == '\0') {
        return;
    }

    char *tok = strtok(line, " ");
    if (!tok) {
        return;
    }

    if (strcmp(tok, "help") == 0) {
        print_help();
    } else if (strcmp(tok, "mac") == 0) {
        uint8_t own[6], peer[6];
        espnow_link_get_own_mac(own);
        espnow_link_get_peer(peer);
        print_mac("own ", own);
        print_mac("peer", peer);
    } else if (strcmp(tok, "peer") == 0) {
        char *arg = strtok(NULL, " ");
        uint8_t mac[6];
        if (!arg || parse_mac(arg, mac) != 0) {
            printf("usage: peer aa:bb:cc:dd:ee:ff\n");
            return;
        }
        espnow_link_set_peer(mac);
    } else if (strcmp(tok, "role") == 0) {
        char *arg = strtok(NULL, " ");
        if (arg && strcmp(arg, "cubesat") == 0) {
            app_tm_tc_set_role(SAT_ROLE_CUBESAT);
        } else if (arg && strcmp(arg, "gcs") == 0) {
            app_tm_tc_set_role(SAT_ROLE_GCS);
        } else {
            printf("usage: role cubesat|gcs\n");
        }
    } else if (strcmp(tok, "hk") == 0) {
        app_tm_tc_send_hk();
    } else if (strcmp(tok, "ping") == 0) {
        app_tm_tc_send_ping();
    } else if (strcmp(tok, "led") == 0) {
        char *arg = strtok(NULL, " ");
        int on = arg && (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0);
        if (app_tm_tc_role() == SAT_ROLE_GCS) {
            app_tm_tc_send_led((uint8_t)on);
        } else {
            gpio_set_level(SAT_LED_GPIO, on ? 1 : 0);
            printf("local LED %s\n", on ? "on" : "off");
        }
    } else if (strcmp(tok, "interval") == 0) {
        char *arg = strtok(NULL, " ");
        if (!arg) {
            printf("usage: interval <ms>\n");
            return;
        }
        app_tm_tc_set_interval((uint32_t)strtoul(arg, NULL, 10));
    } else if (strcmp(tok, "emu") == 0) {
        char *sub = strtok(NULL, " ");
        channel_cfg_t c;
        channel_emu_get(&c);
        if (!sub || strcmp(sub, "show") == 0) {
            printf("emu delay=%lu drop=%u/1000 ber_e6=%lu\n",
                   (unsigned long)c.delay_ms, (unsigned)c.drop_permille,
                   (unsigned long)c.ber_e6);
            return;
        }
        char *val = strtok(NULL, " ");
        if (!val) {
            printf("usage: emu delay|drop|ber <n>\n");
            return;
        }
        unsigned long n = strtoul(val, NULL, 10);
        if (strcmp(sub, "delay") == 0) {
            c.delay_ms = (uint32_t)n;
        } else if (strcmp(sub, "drop") == 0) {
            c.drop_permille = (uint16_t)n;
        } else if (strcmp(sub, "ber") == 0) {
            c.ber_e6 = (uint32_t)n;
        } else {
            printf("usage: emu delay|drop|ber|show\n");
            return;
        }
        channel_emu_set(&c);
        channel_emu_get(&c);
        printf("emu delay=%lu drop=%u/1000 ber_e6=%lu\n",
               (unsigned long)c.delay_ms, (unsigned)c.drop_permille,
               (unsigned long)c.ber_e6);
    } else if (strcmp(tok, "replay") == 0) {
        int rc = app_tm_tc_replay_last();
        printf("replay %d\n", rc);
    } else if (strcmp(tok, "stats") == 0) {
        metrics_t m;
        metrics_get(&m);
        printf("tx_ok=%lu tx_fail=%lu emu_drop=%lu rx_ok=%lu auth_fail=%lu replay=%lu other=%lu rssi=%d enc_us=%lu dec_us=%lu\n",
               (unsigned long)m.tx_ok, (unsigned long)m.tx_fail,
               (unsigned long)m.tx_dropped_emu, (unsigned long)m.rx_ok,
               (unsigned long)m.rx_auth_fail, (unsigned long)m.rx_replay,
               (unsigned long)m.rx_other, (int)m.last_rssi,
               (unsigned long)m.last_enc_us, (unsigned long)m.last_dec_us);
    } else {
        printf("unknown command, try help\n");
        ESP_LOGD(TAG, "unknown: %s", tok);
    }
}

void cli_run(void)
{
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    print_help();
    char line[160];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        handle_line(line);
    }
}
