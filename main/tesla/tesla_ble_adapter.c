/*
 * Tesla BLE adapter — Phase 1 scan-only observer.
 *
 * Owns the scan path entirely: a dedicated task waits for the NimBLE host to
 * sync, then starts a continuous passive scan. The scan callback parses the
 * advertisement name and logs it with the MAC when it matches either Tesla
 * name format (tesla_advert_name.c).
 *
 * This module must stay independent of main/ble/ble_server.c (per the plan's
 * ADR review note 2): the server's gap_event_handler handles peripheral
 * connection/advertising events only; this file's gap_event_handler handles
 * central scan reports only. No cross-feeding either direction.
 */

#include "tesla_ble_adapter.h"
#include "tesla_advert_name.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"

#include <string.h>

static const char *TAG = "tesla_ble";

static int discovery_event_handler(struct ble_gap_event *event, void *arg)
{
    static const char *fmt_str[] = { "none", "legacy", "modern" };
    const struct ble_gap_disc_desc *disc;
    struct ble_hs_adv_fields fields;
    enum tesla_name_format fmt;
    const uint8_t *mac;

    (void)arg;

    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    disc = &event->disc;

    /* Parse the advertised fields; we only care about the local name. */
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return 0;
    }
    if (fields.name == NULL || fields.name_len == 0) {
        return 0;
    }

    fmt = tesla_advert_name_format(fields.name, fields.name_len);
    if (fmt == TESLA_NAME_NONE) {
        return 0;
    }

    /* Advertised bytes are not NUL-terminated: print with a length. MAC is
     * little-endian in ble_addr_t; print big-endian like the phone apps do. */
    mac = disc->addr.val;
    ESP_LOGI(TAG, "Tesla vehicle found: name=\"%.*s\" (format=%s), "
                  "MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             (int)fields.name_len, (const char *)fields.name,
             fmt_str[fmt], mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    return 0;
}

static esp_err_t start_scan(void)
{
    struct ble_gap_disc_params params;
    uint8_t own_addr_type;
    int rc;

    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "scan start skipped: BLE host not synced");
        return ESP_ERR_INVALID_STATE;
    }

    /* Best available address (typically static-random, as ble_server does). */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    memset(&params, 0, sizeof(params));
    params.passive = 1;            /* observer: no active scan requests     */
    params.filter_duplicates = 1;  /* one report per device while scanning  */
    params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;

    /* BLE_HS_FOREVER = no expiration: the observer runs until reboot. */
    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params,
                      discovery_event_handler, NULL);
    if (rc == BLE_HS_EALREADY) {
        return ESP_OK;  /* already scanning: keep the existing scan going */
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "scan start failed: rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "scanning for Tesla advertisements (observer)");
    return ESP_OK;
}

static void scan_wait_task(void *arg)
{
    (void)arg;

    /* The host syncs shortly after nimble_port_init(); poll until it does,
     * then start one infinite scan. A task is used instead of hooking the
     * host's single sync_cb so this adapter needs no coordination with the
     * peripheral server's on_sync(). */
    while (!ble_hs_synced()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (start_scan() != ESP_OK) {
        /* Role misconfiguration or transient host failure; log loudly. */
        ESP_LOGE(TAG, "Tesla observer failed to start scanning");
    }
    vTaskDelete(NULL);
}

esp_err_t tesla_ble_adapter_init(void)
{
    if (xTaskCreate(scan_wait_task, "tesla_scan", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create scan task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}