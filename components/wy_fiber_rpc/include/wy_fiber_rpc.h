/*
 * wy_fiber_rpc.h — Fiber payment channel RPC client for ESP-IDF
 * ==============================================================
 * Wraps the fnn JSON-RPC 2.0 API for payment operations.
 * All calls are synchronous (blocking). Designed for ESP32-P4
 * but compatible with any ESP32 variant with enough heap.
 *
 * RPC methods covered:
 *   send_payment       — pay a BOLT11-style Fiber invoice
 *   get_payment        — poll payment status by hash
 *   new_invoice        — generate a receivable invoice (provider role)
 *   node_info          — sanity-check node connectivity
 *
 * No heap allocations survive past each function call.
 * All output buffers are caller-owned fixed-size arrays.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Payment status ───────────────────────────────────────────── */
typedef enum {
    WY_PAYMENT_INFLIGHT  = 0,
    WY_PAYMENT_SUCCESS   = 1,
    WY_PAYMENT_FAILED    = 2,
    WY_PAYMENT_UNKNOWN   = 3,
} wy_payment_status_t;

/* ── send_payment ─────────────────────────────────────────────── */
/*
 * Pay a Fiber invoice.
 *
 * @param rpc_url        fnn RPC endpoint, e.g. "http://192.168.1.10:8227"
 * @param invoice        Fiber BOLT11 invoice string (fibt1...)
 * @param payment_hash   OUT: 66-char hex string "0x..." (null-terminated)
 * @param hash_len       size of payment_hash buffer (>=67 bytes)
 *
 * Returns ESP_OK if the payment was accepted by fnn (inflight).
 * Does NOT mean payment is settled — call wy_fiber_get_payment() to confirm.
 */
esp_err_t wy_fiber_send_payment(const char *rpc_url,
                                const char *invoice,
                                char       *payment_hash,
                                size_t      hash_len);

/* ── get_payment ──────────────────────────────────────────────── */
/*
 * Poll payment status by hash.
 *
 * @param rpc_url        fnn RPC endpoint
 * @param payment_hash   hex hash returned by wy_fiber_send_payment()
 * @param status_out     OUT: current payment status
 * @param preimage_out   OUT: payment preimage if SUCCESS (66-char hex, or empty)
 * @param preimage_len   size of preimage_out buffer (>=67 bytes)
 */
esp_err_t wy_fiber_get_payment(const char          *rpc_url,
                               const char          *payment_hash,
                               wy_payment_status_t *status_out,
                               char                *preimage_out,
                               size_t               preimage_len);

/* ── send_payment + poll loop ─────────────────────────────────── */
/*
 * Pay an invoice and wait for settlement (blocking, with timeout).
 *
 * @param rpc_url        fnn RPC endpoint
 * @param invoice        Fiber invoice string
 * @param preimage_out   OUT: payment preimage on success (66-char hex)
 * @param preimage_len   size of preimage_out buffer
 * @param timeout_ms     max wait time in milliseconds
 *
 * Returns ESP_OK only when payment is fully settled (preimage received).
 * Returns ESP_ERR_TIMEOUT if not settled within timeout_ms.
 * Returns ESP_FAIL on RPC or payment error.
 */
esp_err_t wy_fiber_pay_and_wait(const char *rpc_url,
                                const char *invoice,
                                char       *preimage_out,
                                size_t      preimage_len,
                                uint32_t    timeout_ms);

/* ── new_invoice ──────────────────────────────────────────────── */
/*
 * Generate a new invoice to receive payment (provider role).
 *
 * @param rpc_url        fnn RPC endpoint
 * @param amount_shannons  amount in shannons (1 CKB = 10^8 shannons)
 * @param description    human-readable description (max 64 chars)
 * @param invoice_out    OUT: invoice string buffer
 * @param invoice_len    size of invoice_out buffer (>=512 bytes recommended)
 * @param payment_hash_out  OUT: payment hash for this invoice (66-char hex)
 * @param hash_len       size of payment_hash_out buffer
 */
esp_err_t wy_fiber_new_invoice(const char *rpc_url,
                               uint64_t    amount_shannons,
                               const char *description,
                               char       *invoice_out,
                               size_t      invoice_len,
                               char       *payment_hash_out,
                               size_t      hash_len);

/* ── node_info ────────────────────────────────────────────────── */
/*
 * Fetch node info — use to verify RPC connectivity at startup.
 *
 * @param rpc_url        fnn RPC endpoint
 * @param node_id_out    OUT: node public key hex (68-char, or empty on fail)
 * @param node_id_len    size of node_id_out buffer
 */
esp_err_t wy_fiber_node_info(const char *rpc_url,
                             char       *node_id_out,
                             size_t      node_id_len);

#ifdef __cplusplus
}
#endif
