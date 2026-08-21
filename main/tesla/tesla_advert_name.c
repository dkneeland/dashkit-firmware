/* Tesla BLE advertisement-name matcher. See tesla_advert_name.h. */

#include "tesla_advert_name.h"

static int is_hex(unsigned char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int tesla_vin_char(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return 1;
    }
    if (c < 'A' || c > 'Z') {
        return 0;
    }
    /* VINs never use I, O, or Q. */
    return (c != 'I' && c != 'O' && c != 'Q');
}

/* Legacy: "S" + first 16 hex chars of SHA1(VIN) + role letter (C/R/D/P),
 * 18 chars total. e.g. 5YJ3E1EB8TF024681 -> S1481f4f405d98dfeC. Only the
 * 18-char form counts; the dev-test 8-hex (10-char) form is not a broadcast. */
static int name_is_legacy(const uint8_t *name, size_t len)
{
    size_t i;

    if (len != 18) {
        return 0;
    }
    if (name[0] != 'S') {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        if (!is_hex(name[1 + i])) {
            return 0;
        }
    }
    switch (name[len - 1]) {
    case 'C':
    case 'R':
    case 'D':
    case 'P':
        return 1;
    default:
        return 0;
    }
}

/* Modern: "Tesla " + 4..6 VIN-alphabet chars (some vehicles use a shorter
 * suffix than the documented 6). Every byte must be a valid VIN character so
 * random "Tesla ..." names can never false-positive. */
static int name_is_modern(const uint8_t *name, size_t len)
{
    static const char prefix[] = "Tesla ";
    const size_t prefix_len = sizeof(prefix) - 1;  /* 6, excludes the NUL */
    size_t i;

    if (len < prefix_len + 4 || len > prefix_len + 6) {
        return 0;
    }
    for (i = 0; i < prefix_len; i++) {
        if (name[i] != (uint8_t)prefix[i]) {
            return 0;
        }
    }
    for (; i < len; i++) {
        if (!tesla_vin_char(name[i])) {
            return 0;
        }
    }
    return 1;
}

enum tesla_name_format tesla_advert_name_format(const uint8_t *name, size_t len)
{
    if (name == NULL || len == 0) {
        return TESLA_NAME_NONE;
    }
    if (name_is_legacy(name, len)) {
        return TESLA_NAME_LEGACY;
    }
    if (name_is_modern(name, len)) {
        return TESLA_NAME_MODERN;
    }
    return TESLA_NAME_NONE;
}