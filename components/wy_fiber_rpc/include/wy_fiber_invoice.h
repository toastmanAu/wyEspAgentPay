/**
 * wy_fiber_invoice.h — Fiber Network invoice decoder for ESP32
 * =============================================================
 * Decodes Fiber invoice strings (fibt1.../fibb1.../fibd1...) into
 * their component fields without any dynamic allocation or external libs.
 *
 * Fiber invoices are BOLT11-derived with two key differences:
 *   1. Currency prefix: fibb/fibt/fibd (not lnbc/lntb)
 *   2. Data part is arithmetically compressed before bech32 encoding
 *      (arcode crate, adaptive Model, 48-bit precision, MSB-first bits)
 *
 * This decoder:
 *   bech32_decode → ar_decompress → protobuf-lite parse → extract fields
 *
 * Tested parameters (from Fiber source crates/fiber-lib/src/invoice/utils.rs):
 *   Model: num_bits=8 (257 symbols: 0-255 + EOF=256), EOFKind::EndAddOne
 *   Precision: 48 bits
 *   Bit order: MSB first
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ─────────────────────────────────────────────────────── */
#define WY_INV_ERR_BASE         0x9100
#define WY_INV_ERR_PREFIX       (WY_INV_ERR_BASE + 1)  /* unknown currency prefix */
#define WY_INV_ERR_BECH32       (WY_INV_ERR_BASE + 2)  /* bech32 decode failed */
#define WY_INV_ERR_DECOMPRESS   (WY_INV_ERR_BASE + 3)  /* ar_decompress failed */
#define WY_INV_ERR_PARSE        (WY_INV_ERR_BASE + 4)  /* field parse error */
#define WY_INV_ERR_BUFFER       (WY_INV_ERR_BASE + 5)  /* output buffer too small */
#define WY_INV_ERR_CHECKSUM     (WY_INV_ERR_BASE + 6)  /* bech32 checksum mismatch */

/* ── Currency ────────────────────────────────────────────────────────── */
typedef enum {
    WY_FIBER_MAINNET  = 0,  /* fibb */
    WY_FIBER_TESTNET  = 1,  /* fibt */
    WY_FIBER_DEVNET   = 2,  /* fibd */
} wy_fiber_currency_t;

/* ── Decoded invoice ─────────────────────────────────────────────────── */
typedef struct {
    wy_fiber_currency_t currency;
    uint64_t            amount_shannons;   /* 0 = unspecified */
    bool                has_amount;
    uint8_t             payment_hash[32];  /* 32 raw bytes */
    char                payment_hash_hex[65]; /* null-terminated hex */
    uint8_t             payment_secret[32];   /* BOLT11 's' field */
    bool                has_payment_secret;
    char                description[256];     /* human-readable memo */
    uint64_t            timestamp;            /* seconds since epoch */
    uint64_t            expiry_seconds;       /* 0 = default 3600 */
} wy_fiber_invoice_t;

/**
 * @brief Decode a Fiber invoice string into its component fields.
 *
 * @param invoice_str   Fiber invoice string (e.g. "fibt1...")
 * @param out           Populated on success
 *
 * @return ESP_OK on success, WY_INV_ERR_* on failure
 */
esp_err_t wy_fiber_invoice_decode(const char *invoice_str, wy_fiber_invoice_t *out);

/**
 * @brief Verify payment proof against a decoded invoice (offline, no RPC).
 *        Checks SHA256(preimage) == payment_hash.
 *
 * @param invoice       Previously decoded invoice
 * @param preimage_hex  64-char hex preimage from X-Payment-Proof header
 *
 * @return ESP_OK if preimage is valid for this invoice
 */
esp_err_t wy_fiber_verify_preimage(const wy_fiber_invoice_t *invoice,
                                    const char               *preimage_hex);

#ifdef __cplusplus
}
#endif
