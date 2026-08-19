#include "tesla_ble_storage.h"

#include "nvs_flash.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>

static const char *TAG        = "tesla_storage";
static const char *NVS_NS     = "tesla";
static const char *KEY_PRIV   = "priv";
static const char *KEY_PUB    = "pub";
static const char *KEY_VIN    = "vin";
static const char *KEY_ADDR   = "addr";
static const char *KEY_BEACON = "beacon_log";

bool tesla_storage_has_key(void)
{
    nvs_handle_t h;
    size_t len = 0;

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e = nvs_get_blob(h, KEY_PRIV, NULL, &len);
    nvs_close(h);
    return e == ESP_OK && len == 32;
}

esp_err_t tesla_storage_load_key(tesla_keypair_t *key)
{
    nvs_handle_t h;
    esp_err_t err;

    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t n = sizeof(key->priv);
    err = nvs_get_blob(h, KEY_PRIV, key->priv, &n);
    if (err == ESP_OK && n != sizeof(key->priv)) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (err == ESP_OK) {
        n = sizeof(key->pub);
        err = nvs_get_blob(h, KEY_PUB, key->pub, &n);
        if (err == ESP_OK && n != sizeof(key->pub)) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        }
    }
    nvs_close(h);
    return err;
}

esp_err_t tesla_storage_save_key(const tesla_keypair_t *key)
{
    nvs_handle_t h;
    esp_err_t err;

    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_PRIV, key->priv, sizeof(key->priv));
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_PUB, key->pub, sizeof(key->pub));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t tesla_storage_load_vin(char *vin, size_t cap)
{
    nvs_handle_t h;
    esp_err_t err;

    if (vin == NULL || cap < 18) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = cap;
    err = nvs_get_str(h, KEY_VIN, vin, &len);
    nvs_close(h);
    return err;
}

esp_err_t tesla_storage_save_vin(const char *vin)
{
    nvs_handle_t h;
    esp_err_t err;

    if (vin == NULL || strlen(vin) != 17) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, KEY_VIN, vin);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t tesla_storage_load_car_addr(tesla_car_addr_t *addr)
{
    nvs_handle_t h;
    esp_err_t err;

    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t n = sizeof(*addr);
    err = nvs_get_blob(h, KEY_ADDR, addr, &n);
    if (err == ESP_OK && n != sizeof(*addr)) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    nvs_close(h);
    return err;
}

esp_err_t tesla_storage_save_car_addr(const tesla_car_addr_t *addr)
{
    nvs_handle_t h;
    esp_err_t err;

    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_ADDR, addr, sizeof(*addr));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void tesla_storage_erase_all(void)
{
    nvs_handle_t h;

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ---- Onboard beacon log ----
//
// Persisted as a single NVS blob (a fixed ring), rewritten only when a NEW
// Beacon is seen (dedup by MAC+name), so normal repetitive advertising does not
// churn NVS. Ring keeps the most recent TESLA_BEACON_LOG_MAX unique cars.

#define BEACON_LOG_MAGIC 0xB0CEB00Bu
static const uint32_t BEACON_LOG_TOTAL =
    (sizeof(tesla_beacon_log_entry_t) * TESLA_BEACON_LOG_MAX) + 8;

typedef struct {
    uint32_t magic;
    uint16_t count;
    uint16_t max;
    tesla_beacon_log_entry_t e[TESLA_BEACON_LOG_MAX];
} __attribute__((packed)) tesla_beacon_log_t;

static const char *fmt_name(uint8_t format)
{
    return format == 2 ? "modern" : (format == 1 ? "legacy" : "none");
}

static void beacon_log_load(nvs_handle_t h, tesla_beacon_log_t *log)
{
    size_t n = BEACON_LOG_TOTAL;
    memset(log, 0, BEACON_LOG_TOTAL);
    if (nvs_get_blob(h, KEY_BEACON, log, &n) != ESP_OK ||
        log->magic != BEACON_LOG_MAGIC || log->max != TESLA_BEACON_LOG_MAX ||
        log->count > TESLA_BEACON_LOG_MAX) {
        log->magic = BEACON_LOG_MAGIC;
        log->max   = TESLA_BEACON_LOG_MAX;
        log->count = 0;
        memset(log->e, 0, sizeof(log->e));
    }
}

void tesla_beacon_log_add(const uint8_t *name, size_t name_len, uint8_t format,
                          const uint8_t mac[6], int8_t rssi)
{
    tesla_beacon_log_t log;
    nvs_handle_t h;

    if (name == NULL || mac == NULL) {
        return;
    }
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    beacon_log_load(h, &log);

    // Dedup: a car advertises the same MAC + name repeatedly; log it once.
    for (uint16_t i = 0; i < log.count; i++) {
        if (log.e[i].name_len == (uint8_t)name_len &&
            log.e[i].format == format &&
            memcmp(log.e[i].mac, mac, 6) == 0 &&
            memcmp(log.e[i].name, name, name_len) == 0) {
            nvs_close(h);
            return;
        }
    }

    // Ring append (drop oldest when full).
    if (log.count >= TESLA_BEACON_LOG_MAX) {
        memmove(&log.e[0], &log.e[1],
                sizeof(tesla_beacon_log_entry_t) * (TESLA_BEACON_LOG_MAX - 1));
    } else {
        log.count++;
    }
    tesla_beacon_log_entry_t *en = &log.e[log.count - 1];
    memset(en, 0, sizeof(*en));
    size_t n = name_len < sizeof(en->name) ? name_len : sizeof(en->name);
    memcpy(en->name, name, n);
    en->name_len = (uint8_t)n;
    en->format   = format;
    memcpy(en->mac, mac, 6);
    en->rssi     = rssi;
    en->time_s   = (uint32_t)(esp_timer_get_time() / 1000000);

    if (nvs_set_blob(h, KEY_BEACON, &log, BEACON_LOG_TOTAL) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

void tesla_beacon_log_dump(void)
{
    tesla_beacon_log_t log;
    nvs_handle_t h;

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "beacon log: (no NVS)");
        return;
    }
    beacon_log_load(h, &log);
    nvs_close(h);

    ESP_LOGI(TAG, "beacon log: %u previous Tesla detection(s) (oldest->newest)", log.count);
    for (uint16_t i = 0; i < log.count; i++) {
        const tesla_beacon_log_entry_t *en = &log.e[i];
        ESP_LOGI(TAG, "  #%u name=\"%.*s\" (format=%s) MAC=%02X:%02X:%02X:%02X:%02X:%02X "
                      "rssi=%d t=%us",
                 (unsigned)i + 1, (int)en->name_len, (const char *)en->name,
                 fmt_name(en->format),
                 en->mac[5], en->mac[4], en->mac[3], en->mac[2], en->mac[1], en->mac[0],
                 (int)en->rssi, (unsigned)en->time_s);
    }
}

void tesla_beacon_log_clear(void)
{
    nvs_handle_t h;

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, KEY_BEACON);
        nvs_commit(h);
        nvs_close(h);
    }
}
