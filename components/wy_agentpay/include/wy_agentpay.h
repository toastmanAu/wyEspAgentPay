/**
 * wy_agentpay.h — Top-level AgentPay client API
 * ===============================================
 * Single include for integrating wyEspAgentPay into a project.
 *
 * Usage (payer side — device calls paid API):
 *
 *   wy_agentpay_config_t cfg = {
 *       .fiber_rpc_url = "http://192.168.1.1:8227",
 *       .payment_timeout_ms = 30000,
 *   };
 *   wy_agentpay_init(&cfg);
 *
 *   char resp[2048];
 *   int status;
 *   esp_err_t err = wy_agentpay_call("https://api.example.com/data",
 *                                     "GET", NULL, resp, sizeof(resp), &status);
 *
 * Usage (provider side — device serves paid resource):
 *
 *   wy_agentpay_server_t srv;
 *   wy_agentpay_server_init(&srv, &cfg, 100);  // 100 shannons per request
 *   // In your httpd handler:
 *   wy_payment_result_t pay;
 *   esp_err_t err = wy_agentpay_server_await_payment(&srv, req, &pay);
 *   if (err == ESP_OK) { // payment confirmed, send resource }
 */
#pragma once

#include "esp_err.h"
#include "wy_fiber_rpc.h"
#include "wy_x402.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Config — no hardcoded secrets, all runtime-provided ──────── */

typedef struct {
    char     fiber_rpc_url[256];    /* local fnn RPC endpoint */
    uint32_t payment_timeout_ms;    /* 0 = 30s default */
    char     node_description[64];  /* optional, used in invoices */
} wy_agentpay_config_t;

/* ── Payer API ─────────────────────────────────────────────────── */

/**
 * @brief Initialise the AgentPay client. Must be called before wy_agentpay_call().
 *        Verifies connectivity to fnn node and logs node_id/version.
 */
esp_err_t wy_agentpay_init(const wy_agentpay_config_t *cfg);

/**
 * @brief Make an HTTP call, paying automatically if a 402 is returned.
 *        Wraps wy_x402_perform with the stored config.
 *
 * @param url           Target URL
 * @param method        "GET" or "POST"
 * @param post_body     JSON body for POST (NULL for GET)
 * @param resp_buf      Response body buffer
 * @param resp_len      Buffer size
 * @param http_status   Final HTTP status (out)
 *
 * @return ESP_OK on success, WY_FIBER_ERR_* on payment failure
 */
esp_err_t wy_agentpay_call(const char *url,
                            const char *method,
                            const char *post_body,
                            char       *resp_buf,
                            size_t      resp_len,
                            int        *http_status);

/* ── Provider (server) API ─────────────────────────────────────── */

typedef struct {
    wy_agentpay_config_t cfg;
    uint64_t             price_shannons;   /* cost per request */
} wy_agentpay_server_t;

/**
 * @brief Initialise the server-side context.
 *
 * @param srv              Server context to populate
 * @param cfg              AgentPay config (fiber_rpc_url etc.)
 * @param price_shannons   Amount to charge per request (1 CKB = 100,000,000)
 */
esp_err_t wy_agentpay_server_init(wy_agentpay_server_t       *srv,
                                   const wy_agentpay_config_t *cfg,
                                   uint64_t                    price_shannons);

/**
 * @brief Generate a 402 response payload for an incoming request.
 *        Call this when your httpd handler wants to demand payment.
 *        Returns JSON: {"payment":{"fiber_invoice":"fibn1...","amount_shannons":N}}
 *
 * @param srv         Server context
 * @param out_buf     Buffer for JSON payload
 * @param out_len     Buffer size
 * @param hash_out    Payment hash for later verification (66 bytes)
 */
esp_err_t wy_agentpay_server_make_challenge(wy_agentpay_server_t *srv,
                                             char                 *out_buf,
                                             size_t                out_len,
                                             char                 *hash_out);

/**
 * @brief Verify X-Payment-Proof header from a retried request.
 *        Checks preimage:hash format and confirms settlement with fnn.
 *
 * @param srv          Server context
 * @param proof_header Value of X-Payment-Proof header
 * @param expected_hash Payment hash issued in the challenge
 *
 * @return ESP_OK if payment confirmed, error otherwise
 */
esp_err_t wy_agentpay_server_verify_proof(wy_agentpay_server_t *srv,
                                           const char           *proof_header,
                                           const char           *expected_hash);

#ifdef __cplusplus
}
#endif
