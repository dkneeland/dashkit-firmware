/*
 * Tesla BLE storage — NVS persistence for the vehicle-command client state.
 *
 * Namespace "tesla" holds the pieces the Phase 2/3 central client needs across
 * reboots so it does not re-pair on every power cycle:
 *   - the enrolled client keypair (private + public scalar/point, raw bytes)
 *   - the 17-char VIN (personalization for every command)
 *   - the car's BLE address (so we connect directly instead of re-scanning)
 *
 * Phase 3 (pairing) fills these via tesla_storage_save_*; Phase 2 code reads
 * them (tesla_storage_load_*) so that once a key is enrolled the handshake +
 * GET_STATUS poll can run. Session caching (epoch/counter/clock offset) and
 * key generation/re-enrollment land in Phase 3.
 *
 * Matches the plan: "plaintext NVS private key initially (matches the ESPHome
 * reference)" — no key material is ever logged; flash-encryption / SE hardening
 * is the documented release-blocker risk (plan §7).
 *
 * RELEASE BLOCKER (do not ship a DRIVER-role build with this): the private key
 * lives plaintext in NVS. Flash encryption and/or a secure-element-backed key
 * must land before any production/DRIVER-role release (plan §7 / ADR §). See
 * plan §7 "release-blocker candidate".
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

// BLE address the client connects to (fields match ble_addr_t layout: a type
// byte followed by the 6 MAC bytes, big-endian).
typedef struct {
    uint8_t type;
    uint8_t val[6];
} tesla_car_addr_t;

// --- keypair ---
bool tesla_storage_has_key(void);
esp_err_t tesla_storage_load_key(tesla_keypair_t *key);
esp_err_t tesla_storage_save_key(const tesla_keypair_t *key);

// --- VIN (17 chars, NUL-terminated) ---
esp_err_t tesla_storage_load_vin(char *vin, size_t cap);
esp_err_t tesla_storage_save_vin(const char *vin);

// --- car BLE address ---
esp_err_t tesla_storage_load_car_addr(tesla_car_addr_t *addr);
esp_err_t tesla_storage_save_car_addr(const tesla_car_addr_t *addr);

// Remove all Tesla state (used by a factory reset / re-pair flow in Phase 3+).
void tesla_storage_erase_all(void);

// ---- Onboard Tesla-beacon detection log (in-car trust test) ----
//
// The central adapter records every Tesla-format match (dedup by MAC+name) into
// an NVS ring so you can power the board near a car with NO live serial monitor
// and read what it saw afterwards: on the next boot tesla_beacon_log_dump()
// prints the previous run's detections to serial. This is a lightweight test
// aid, not a telemetry subsystem.
#define TESLA_BEACON_LOG_MAX 32
typedef struct {
    uint8_t  name[16];
    uint8_t  name_len;
    uint8_t  format;      // tesla_name_format (1=legacy, 2=modern)
    uint8_t  mac[6];
    int8_t   rssi;        // dBm
    uint8_t  _pad;
    uint32_t time_s;      // seconds since boot when seen
} __attribute__((packed)) tesla_beacon_log_entry_t;

void tesla_beacon_log_add(const uint8_t *name, size_t name_len, uint8_t format,
                          const uint8_t mac[6], int8_t rssi);
void tesla_beacon_log_dump(void);
void tesla_beacon_log_clear(void);

#ifdef __cplusplus
}
#endif
