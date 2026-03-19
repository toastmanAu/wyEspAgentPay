/*
 * wy_x402.h — HTTP 402 Payment Required intercept for ESP-IDF
 * ============================================================
 * Intercepts a 402 response, parses the Fiber invoice, pays it
 * via wy_fiber_rpc, then signals the caller to retry with the
 * correct X-Payment-Proof header.
 *
 * x402 proof format (standard):  preimage:payment_hash
 * Header:                         X-Payment-Proof: <proof>
 *
 * Usage pattern:
 *   1. Make your HTTP request
 *   2. If status == 402, call wy_x402_handle()
 *   3. If ESP_OK, set X-Payment-Proof header and retry request
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Handle a 402 response.
 *
 * Reads the 402 body, extracts the Fiber invoice, pays it via fnn,
 * waits for settlement, and builds the X-Payment-Proof header value.
 *
 * @param client         ESP HTTP client handle (after perform(), status=402)
 * @param fiber_rpc_url  fnn RPC endpoint, e.g. "http://192.168.1.10:8227"
 * @param proof_out      OUT: proof string for X-Payment-Proof header
 *                            format: "preimage:payment_hash" (null-terminated)
 * @param proof_len      size of proof_out (>=140 bytes recommended)
 * @param timeout_ms     max wait for payment settlement
 *
 * Returns ESP_OK if payment settled and proof_out is populated.
 */
esp_err_t wy_x402_handle(esp_http_client_handle_t client,
                         const char              *fiber_rpc_url,
                         char                    *proof_out,
                         size_t                   proof_len,
                         uint32_t                 timeout_ms);

/*
 * Expected 402 body JSON schema:
 * {
 *   "x402Version": 1,
 *   "payment": {
 *     "scheme": "fiber",
 *     "network": "testnet",
 *     "fiber_invoice": "fibt1...",
 *     "amount_shannons": 1000,
 *     "description": "..."
 *   }
 * }
 */

#ifdef __cplusplus
}
#endif
