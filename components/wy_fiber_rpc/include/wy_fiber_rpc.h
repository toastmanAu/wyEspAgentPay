/**
 * wy_fiber_rpc.h — Fiber Network JSON-RPC client for ESP32
 * =========================================================
 * Wraps fnn v0.7.x RPC methods needed for x402 payment flows.
 * All calls are synchronous (blocking). No dynamic allocation
 * beyond cJSON internals — safe for ESP32 heap constraints.
 *
 * Tested against fnn v0.7.0 (ckbnode, 192.168.68.105:8227)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ─────────────────────────────────────────────── */
#define WY_FIBER_ERR_BASE          0x9000
#define WY_FIBER_ERR_RPC_FAIL      (WY_FIBER_ERR_BASE + 1)  /* HTTP/transport error */
#define WY_FIBER_ERR_RPC_ERROR     (WY_FIBER_ERR_BASE + 2)  /* fnn returned {error:...} */
#define WY_FIBER_ERR_PARSE         (WY_FIBER_ERR_BASE + 3)  /* JSON parse failure */
#define WY_FIBER_ERR_TIMEOUT       (WY_FIBER_ERR_BASE + 4)  /* payment poll timed out */
#define WY_FIBER_ERR_REJECTED      (WY_FIBER_ERR_BASE + 5)  /* payment failed/rejected */
#define WY_FIBER_ERR_BUFFER        (WY_FIBER_ERR_BASE + 6)  /* output buffer too small */

/* ── Payment status ──────────────────────────────────────────── */
typedef enum {
    WY_PAYMENT_PENDING = 0,
    WY_PAYMENT_SUCCESS,
    WY_PAYMENT_FAILED,
    WY_PAYMENT_UNKNOWN,
} wy_payment_status_t;

/* ── send_payment result ─────────────────────────────────────── */
typedef struct {
    char payment_hash[66];   /* hex, 32 bytes = 64 chars + null */
    char preimage[66];       /* hex, revealed on success */
    wy_payment_status_t status;
} wy_payment_result_t;

/* ── node_info result (subset) ───────────────────────────────── */
typedef struct {
    char node_id[68];        /* 33-byte compressed pubkey, hex */
    char version[32];
    uint32_t channel_count;
} wy_node_info_t;

/**
 * @brief Query fnn node info (connectivity check / channel count).
 *
 * @param rpc_url   Full URL of fnn RPC, e.g. "http://192.168.1.1:8227"
 * @param out       Populated on success
 */
esp_err_t wy_fiber_node_info(const char *rpc_url, wy_node_info_t *out);

/**
 * @brief Send a payment for a BOLT11-style Fiber invoice.
 *        Blocks until fnn reports success/failure or timeout.
 *
 * @param rpc_url       fnn RPC URL
 * @param invoice       Fiber invoice string (fibn1...)
 * @param timeout_ms    Max wait for payment to settle (ms), 0 = default 30s
 * @param result        Populated with payment_hash, preimage, status on return
 */
esp_err_t wy_fiber_send_payment(const char *rpc_url,
                                const char *invoice,
                                uint32_t    timeout_ms,
                                wy_payment_result_t *result);

/**
 * @brief Poll payment status by payment_hash.
 *
 * @param rpc_url       fnn RPC URL
 * @param payment_hash  Hex payment hash from send_payment
 * @param status_out    Current status
 * @param preimage_out  Buffer for preimage (66 bytes), filled on SUCCESS
 * @param preimage_len  Size of preimage_out buffer
 */
esp_err_t wy_fiber_get_payment_status(const char          *rpc_url,
                                      const char          *payment_hash,
                                      wy_payment_status_t *status_out,
                                      char                *preimage_out,
                                      size_t               preimage_len);

/**
 * @brief Create a hold invoice (escrow — provider side).
 *        Used when the ESP32 *receives* payment before delivering a resource.
 *
 * @param rpc_url         fnn RPC URL
 * @param amount_shannons Payment amount in shannons (1 CKB = 10^8 shannons)
 * @param description     Human-readable purpose string
 * @param invoice_out     Buffer for resulting invoice string
 * @param out_len         Size of invoice_out
 */
esp_err_t wy_fiber_new_hold_invoice(const char *rpc_url,
                                    uint64_t    amount_shannons,
                                    const char *description,
                                    char       *invoice_out,
                                    size_t      out_len);

/**
 * @brief Settle a hold invoice by revealing the preimage (provider side).
 *
 * @param rpc_url       fnn RPC URL
 * @param payment_hash  Hash of the hold invoice to settle
 * @param preimage      32-byte preimage hex to reveal
 */
esp_err_t wy_fiber_settle_hold(const char *rpc_url,
                               const char *payment_hash,
                               const char *preimage);

/**
 * @brief Cancel a hold invoice (provider side — payment not made or expired).
 */
esp_err_t wy_fiber_cancel_hold(const char *rpc_url, const char *payment_hash);

#ifdef __cplusplus
}
#endif
