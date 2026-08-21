/*
 * Tesla BLE advertisement-name matcher.
 *
 * A Tesla advertises under one of two documented formats (vehicle-command
 * protocol.md):
 *
 *   Legacy: "S" + first 16 hex chars of SHA1(VIN) + role letter (C/R/D/P),
 *     e.g. 5YJ3E1EB8TF024681 -> S1481f4f405d98dfeC. The 8-hex
 *     (10-char) form is NOT a real broadcast — it was the dev/test fake beacon.
 *   Modern: "Tesla " + last 6 characters of the VIN.
 *
 * Shape-only (no VIN, no crypto), so it compiles into the host test suite
 * unchanged (tools/test/test_tesla_advert_name.c); binding a found vehicle to
 * OUR stored VIN is done by the caller, not here.
 */
#ifndef TESLA_ADVERT_NAME_H
#define TESLA_ADVERT_NAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum tesla_name_format {
    TESLA_NAME_NONE = 0,   /* not a Tesla advertisement name                 */
    TESLA_NAME_LEGACY,     /* S + 16 hex + C/R/D/P (real car); 8-hex is dev beacon only */
    TESLA_NAME_MODERN,     /* "Tesla " + 4..6 VIN-alphabet characters        */
};

/* Classifies a BLE advertisement local name. `name` is the raw bytes of the
 * advertised Name field (length-prefixed, not NUL-terminated) — pure ASCII.
 */
enum tesla_name_format tesla_advert_name_format(const uint8_t *name, size_t len);

/* 1 if c is a valid VIN character: 0-9 or uppercase A-Z excluding I, O, Q. */
int tesla_vin_char(unsigned char c);

#ifdef __cplusplus
}
#endif

#endif /* TESLA_ADVERT_NAME_H */