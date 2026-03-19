/*
 * wy_agentpay.h — Top-level AgentPay API for ESP-IDF
 * ====================================================
 * Single-header interface for adding x402 Fiber micropayments
 * to any ESP-IDF HTTP request. Integrates wy_fiber_rpc + wy_x402.
 *
 * Typical usage (payer role):
 *
 *   wy_agentpay_config_t cfg = {
 *       .fiber_rpc_url  = CONFIG_WY_FIBER_RPC_URL,
 *       .payment_timeout_ms = 30000,
 *   };
 *   wy_agentpay_init(&cfg);
 *
 *   char response[1024];
 *   esp_err_t err = wy_agentpay_get("http://service.local/data",
 *                                   response, sizeof(response));
 *   if (err == ESP_OK) { ... use response ... }
 *
 * Config comes from NVS or Kconfig — no hardcoded secrets.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Config ───────────────────────────────────────────────────── */

typedef struct {
    char     fiber_rpc_url[256];   /* fnn RPC endpoint — from NVS/Kconfig  */
    uint32_t payment_timeout_ms;   /* max wait for settlement (default 30s) */
    uint32_t http_timeout_ms;      /* HTTP request timeout (default 10s)    */
} wy_agentpay_config_t;

/* ── Lifecycle ────────────────────────────────────────────────── */

/*
 * Initialise AgentPay. Call once at startup after WiFi is up.
 * Verifies fnn connectivity via node_info RPC.
 */
esp_err_t wy_agentpay_init(const wy_agentpay_config_t *config);

/* ── Payer API ────────────────────────────────────────────────── */

/*
 * HTTP GET with automatic x402 payment handling.
 *
 * Makes request → if 402, pays invoice → retries with proof header.
 * Fills response_buf with the final (post-payment) response body.
 */
esp_err_t wy_agentpay_get(const char *url,
                          char       *response_buf,
                          size_t      buf_len);

/*
 * HTTP POST with automatic x402 payment handling.
 */
esp_err_t wy_agentpay_post(const char *url,
                           const char *json_body,
                           char       *response_buf,
                           size_t      buf_len);

/* ── Provider API (receive payments) ─────────────────────────── */

/*
 * Generate a 402 response body JSON for a given invoice amount.
 * The server role: call this to build the 402 response payload.
 *
 * @param amount_shannons  how much to charge
 * @param description      what the payment is for
 * @param out_json         OUT: JSON string to send as 402 body
 * @param out_len          size of out_json buffer (>=512 bytes)
 */
esp_err_t wy_agentpay_make_402(uint64_t    amount_shannons,
                               const char *description,
                               char       *out_json,
                               size_t      out_len);

/*
 * Verify an incoming X-Payment-Proof header value.
 * Returns ESP_OK if the preimage is valid for the expected payment_hash.
 *
 * @param proof_header   value of X-Payment-Proof header from request
 * @param expected_hash  payment hash you issued in the 402
 */
esp_err_t wy_agentpay_verify_proof(const char *proof_header,
                                   const char *expected_hash);

#ifdef __cplusplus
}
#endif
