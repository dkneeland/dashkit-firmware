/*
 * Tesla BLE adapter (Phase 1: scan-only observer spike).
 *
 * Phase 1 wires the OBSERVER role: a passive scan that logs the name and MAC of
 * every nearby Tesla advertisement (both name formats). There is no connect,
 * discovery, or session logic yet — that arrives with the CENTRAL role in
 * Phase 2. Deliberately nothing here shares state or handlers with the
 * peripheral GATT server (main/ble/ble_server.c): this module owns its own GAP
 * callback and its own start-up so a central connection event can never be
 * mistaken for a phone connection (ADR 0001 review note 2).
 */
#ifndef TESLA_BLE_ADAPTER_H
#define TESLA_BLE_ADAPTER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the Tesla observer. Safe to call once at boot when
 * CONFIG_DASHKIT_TESLA_BLE is enabled. The scan begins as soon as the NimBLE
 * host is synced and then runs continuously.
 */
esp_err_t tesla_ble_adapter_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TESLA_BLE_ADAPTER_H */