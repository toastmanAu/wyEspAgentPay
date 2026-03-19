/**
 * wy_x402.h — HTTP 402 Payment Required interceptor
 * ==================================================
 * Drop-in wrapper around esp_http_client that handles
 * x402 payment flows transparently:
 *
 *   1. Make HTTP request
 *   2. If 402 → parse Fiber invoice from body
 *   3. Pay via wy_fiber_rpc
 *   4. Retry with X-Payment-Proof header
 *   5. Return final response to caller
 */
#pragma once

#include "esp_err.h"
#include "esp_http_client.h"
#include "wy_fiber_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * x402 config — provided once per request (or stored in agentpay_client).
 * No secrets stored here — fiber_rpc_url points to local/trusted fnn node.
 */
typedef struct {
    const char *fiber_rpc_url;   /* e.g. "http://192.168.1.1:8227" */
    uint32_t    payment_timeout_ms;  /* 0 = default 30s */
} wy_x402_config_t;

/**
 * Expected x402 body schema (JSON):
 * {
 *   "payment": {
 *     "fiber_invoice": "fibn1...",
 *     "amount_shannons": 1000,
 *     "description": "optional"
 *   }
 * }
 */

/**
 * @brief Perform an HTTP request, handling 402 automatically.
 *
 * If the server returns 402, this function:
 *   - Reads and parses the payment details
 *   - Pays via Fiber (blocking until settled or timeout)
 *   - Retries the original request with proof header
 *
 * @param url           Target URL
 * @param method        "GET" or "POST"
 * @param post_body     JSON body for POST (NULL for GET)
 * @param x402_cfg      Fiber payment config
 * @param resp_buf      Buffer for response body
 * @param resp_len      Size of resp_buf
 * @param http_status   Final HTTP status code (out)
 * @param payment_out   Optional — filled with payment details if payment was made
 *
 * @return ESP_OK on successful request (regardless of payment);
 *         WY_FIBER_ERR_* if payment itself failed
 */
esp_err_t wy_x402_perform(const char              *url,
                           const char              *method,
                           const char              *post_body,
                           const wy_x402_config_t  *x402_cfg,
                           char                    *resp_buf,
                           size_t                   resp_len,
                           int                     *http_status,
                           wy_payment_result_t     *payment_out);

#ifdef __cplusplus
}
#endif
