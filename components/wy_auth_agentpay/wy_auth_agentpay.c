/**
 * wy_auth_agentpay.c — WyAuth × wyEspAgentPay integration bridge
 */

#include "wy_auth_agentpay.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "cJSON/cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "wy_auth_agentpay";

#define DEFAULT_TIMEOUT_MS      60000
#define POLL_INTERVAL_MS        2000
#define CRED_CALLBACK_BUF       2048

/* ── Module state ─────────────────────────────────────────────── */

static wa_ap_config_t s_cfg;
static bool           s_initialised = false;

/* Credential callback: JoyID POSTs the credential JSON back to us.
 * A tiny httpd handler receives it, copies into s_cred_buf, then
 * signals s_cred_sem so wa_ap_authenticate() can return. */
static char           s_cred_buf[CRED_CALLBACK_BUF];
static SemaphoreHandle_t s_cred_sem = NULL;
static httpd_handle_t s_httpd       = NULL;

/* ── Credential callback HTTP handler ─────────────────────────── */

static esp_err_t _cred_callback_handler(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len >= CRED_CALLBACK_BUF) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }

    memset(s_cred_buf, 0, sizeof(s_cred_buf));
    if (httpd_req_recv(req, s_cred_buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv fail");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "{\"ok\":true}");
    xSemaphoreGive(s_cred_sem);
    return ESP_OK;
}

static httpd_uri_t s_cred_uri = {
    .uri      = "/wyauth/callback",
    .method   = HTTP_POST,
    .handler  = _cred_callback_handler,
    .user_ctx = NULL,
};

/* ── Internal helpers ─────────────────────────────────────────── */

/**
 * Extract the CKB address from a raw JoyID credential JSON string.
 * Fills addr_out (up to addr_len bytes including null terminator).
 */
static bool _extract_address(const char *cred_json,
                              char       *addr_out,
                              size_t      addr_len)
{
    cJSON *doc = cJSON_Parse(cred_json);
    if (!doc) return false;

    cJSON *addr = cJSON_GetObjectItemCaseSensitive(doc, "address");
    bool ok = cJSON_IsString(addr) && addr->valuestring
              && strlen(addr->valuestring) < addr_len;
    if (ok) strncpy(addr_out, addr->valuestring, addr_len - 1);

    cJSON_Delete(doc);
    return ok;
}

/**
 * Lightweight P-256 credential verify via WyAuth cJSON path.
 * WyAuth is a C++ Arduino library — we call it here via a thin C shim
 * (wy_auth_verify_shim) declared in wy_auth_shim.h, which wraps
 * JoyIDProvider::verify(). The shim is compiled into the Arduino sketch.
 *
 * If the shim is not linked (pure ESP-IDF project), this returns true
 * unconditionally and logs a warning — address is still usable but
 * cryptographic verification is skipped.
 */
__attribute__((weak))
bool wy_auth_verify_shim(const char *cred_json)
{
    ESP_LOGW(TAG, "wy_auth_verify_shim not linked — skipping P-256 verify");
    return true;
}

/* ── Public API ───────────────────────────────────────────────── */

esp_err_t wa_ap_init(const wa_ap_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memcpy(&s_cfg, cfg, sizeof(wa_ap_config_t));

    if (!s_cred_sem) {
        s_cred_sem = xSemaphoreCreateBinary();
        if (!s_cred_sem) return ESP_ERR_NO_MEM;
    }

    /* Start credential callback httpd if not already running */
    if (!s_httpd) {
        httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
        hcfg.server_port = 8080;
        if (httpd_start(&s_httpd, &hcfg) != ESP_OK) {
            ESP_LOGE(TAG, "httpd start failed");
            return ESP_FAIL;
        }
        httpd_register_uri_handler(s_httpd, &s_cred_uri);
        ESP_LOGI(TAG, "credential callback listening on :8080/wyauth/callback");
    }

    /* Verify Fiber node connectivity */
    wy_node_info_t info = {0};
    esp_err_t err = wy_fiber_node_info(cfg->fiber_rpc_url, &info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fiber node unreachable at %s — Fiber payments unavailable",
                 cfg->fiber_rpc_url);
    } else {
        ESP_LOGI(TAG, "Fiber node: %s  channels=%lu", info.node_id, info.channel_count);
    }

    s_initialised = true;
    return ESP_OK;
}


esp_err_t wa_ap_authenticate(const char *session_id,
                              char       *url_out,
                              size_t      url_len,
                              char       *cred_json_out,
                              size_t      cred_json_len,
                              uint32_t    timeout_ms)
{
    if (!s_initialised) return ESP_ERR_INVALID_STATE;
    if (!session_id || !url_out || !cred_json_out) return ESP_ERR_INVALID_ARG;
    if (timeout_ms == 0) timeout_ms = DEFAULT_TIMEOUT_MS;

    /* Build callback URL: auth_callback_url + /wyauth/callback?session=<id> */
    char callback[384];
    snprintf(callback, sizeof(callback), "%s/wyauth/callback?session=%s",
             s_cfg.auth_callback_url, session_id);

    /* Build JoyID auth URL (mirrors JoyIDProvider::buildAuthURL) */
    /* Base64url-encode the _data_ payload */
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"redirectURL\":\"%s\",\"title\":\"%s\",\"requestNetwork\":\"nervos\"}",
             callback, s_cfg.app_name);

    /* Simple base64url encoding inline */
    static const char *B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char encoded[768] = {0};
    size_t ei = 0;
    const uint8_t *src = (const uint8_t *)payload;
    size_t plen = strlen(payload);
    for (size_t i = 0; i < plen; i += 3) {
        uint32_t b = (uint32_t)src[i] << 16;
        if (i+1 < plen) b |= (uint32_t)src[i+1] << 8;
        if (i+2 < plen) b |= src[i+2];
        encoded[ei++] = B64[(b>>18)&63];
        encoded[ei++] = B64[(b>>12)&63];
        if (i+1 < plen) encoded[ei++] = B64[(b>>6)&63];
        if (i+2 < plen) encoded[ei++] = B64[b&63];
    }

    snprintf(url_out, url_len,
             "https://app.joyid.dev/auth?type=redirect&_data_=%s", encoded);
    ESP_LOGI(TAG, "Auth URL ready (session=%s) — display as QR", session_id);

    /* Wait for credential callback */
    memset(s_cred_buf, 0, sizeof(s_cred_buf));
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_cred_sem, ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Auth timeout after %lums", (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    size_t clen = strlen(s_cred_buf);
    if (clen == 0 || clen >= cred_json_len) return ESP_ERR_INVALID_SIZE;
    memcpy(cred_json_out, s_cred_buf, clen + 1);

    ESP_LOGI(TAG, "Credential received (%d bytes)", (int)clen);
    return ESP_OK;
}


esp_err_t wa_ap_accept_fiber(const char      *cred_json,
                              uint64_t         amount_shannons,
                              wa_ap_payment_t *result)
{
    if (!s_initialised || !cred_json || !result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    /* Extract and verify payer identity */
    if (!_extract_address(cred_json, result->payer_address,
                          sizeof(result->payer_address))) {
        ESP_LOGE(TAG, "Could not extract payer address from credential");
        return ESP_ERR_INVALID_ARG;
    }
    result->identity_verified = wy_auth_verify_shim(cred_json);
    if (!result->identity_verified) {
        ESP_LOGE(TAG, "P-256 signature verification failed for %s",
                 result->payer_address);
        return ESP_ERR_INVALID_STATE;
    }

    /* Generate hold invoice — embed payer address in description so we can
     * correlate identity to payment without a separate lookup */
    char description[192];
    snprintf(description, sizeof(description),
             "wyauth:%s amount:%llu", result->payer_address,
             (unsigned long long)amount_shannons);

    char invoice[512] = {0};
    esp_err_t err = wy_fiber_new_hold_invoice(s_cfg.fiber_rpc_url,
                                              amount_shannons,
                                              description,
                                              invoice, sizeof(invoice));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hold invoice creation failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Hold invoice ready — waiting for payment from %s",
             result->payer_address);

    /* Poll for payment — payer calls wy_fiber_send_payment(invoice) on their end */
    uint32_t timeout_ms = s_cfg.payment_timeout_ms
                        ? s_cfg.payment_timeout_ms : DEFAULT_TIMEOUT_MS;
    uint32_t start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    wy_payment_status_t status = WY_PAYMENT_PENDING;
    char preimage[66] = {0};

    /* Extract payment hash from invoice to poll status */
    /* The invoice encodes the payment hash — wy_fiber_invoice_decode extracts it */
    char payment_hash[66] = {0};
    {
        /* Import the invoice decoder from wy_fiber_invoice */
        extern esp_err_t wy_fiber_invoice_decode(const char *invoice,
                                                  char *payment_hash_out,
                                                  size_t hash_len,
                                                  uint64_t *amount_out);
        uint64_t inv_amount = 0;
        if (wy_fiber_invoice_decode(invoice, payment_hash, sizeof(payment_hash),
                                    &inv_amount) != ESP_OK) {
            ESP_LOGW(TAG, "Could not decode invoice hash — polling by status only");
        } else {
            strncpy(result->payment_hash, payment_hash, sizeof(result->payment_hash));
        }
    }

    while ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start < timeout_ms) {
        err = wy_fiber_get_payment_status(s_cfg.fiber_rpc_url,
                                          payment_hash,
                                          &status,
                                          preimage, sizeof(preimage));
        if (err == ESP_OK && status == WY_PAYMENT_SUCCESS) break;
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }

    if (status != WY_PAYMENT_SUCCESS) {
        ESP_LOGE(TAG, "Payment not received within timeout — cancelling hold");
        wy_fiber_cancel_hold(s_cfg.fiber_rpc_url, payment_hash);
        return WY_FIBER_ERR_TIMEOUT;
    }

    /* Settle: reveal preimage, funds released to us */
    err = wy_fiber_settle_hold(s_cfg.fiber_rpc_url, payment_hash, preimage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settle_hold failed: %d", err);
        return err;
    }

    result->amount_shannons = amount_shannons;
    result->fiber = true;
    ESP_LOGI(TAG, "Fiber payment settled — payer=%s hash=%s",
             result->payer_address, result->payment_hash);
    return ESP_OK;
}


esp_err_t wa_ap_accept_l1(const char      *cred_json,
                            uint64_t         amount_shannons,
                            char            *qr_out,
                            size_t           qr_len,
                            wa_ap_payment_t *result)
{
    if (!s_initialised || !cred_json || !qr_out || !result)
        return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    /* Verify identity */
    if (!_extract_address(cred_json, result->payer_address,
                          sizeof(result->payer_address)))
        return ESP_ERR_INVALID_ARG;

    result->identity_verified = wy_auth_verify_shim(cred_json);
    if (!result->identity_verified) {
        ESP_LOGE(TAG, "P-256 verify failed");
        return ESP_ERR_INVALID_STATE;
    }

    /* Build nervos: payment URI pointing at merchant address */
    double ckb = (double)amount_shannons / 100000000.0;
    snprintf(qr_out, qr_len, "nervos:%s?amount=%.8f", s_cfg.merchant_address, ckb);
    ESP_LOGI(TAG, "L1 payment QR: %s — display to customer", qr_out);

    /* Poll CKB indexer for incoming cell — reuses the same indexer RPC
     * path as CKBPaymentProvider::_checkIncomingCells() */
    uint32_t timeout_ms = s_cfg.payment_timeout_ms
                        ? s_cfg.payment_timeout_ms : DEFAULT_TIMEOUT_MS;
    uint32_t start  = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    /* Get starting tip block number */
    char resp[256] = {0};
    uint64_t from_block = 0;
    {
        esp_http_client_config_t hcfg = {
            .url        = s_cfg.ckb_rpc_url,
            .method     = HTTP_METHOD_POST,
            .timeout_ms = 10000,
        };
        esp_http_client_handle_t hc = esp_http_client_init(&hcfg);
        const char *tip_body =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"get_tip_block_number\","
            "\"params\":[]}";
        esp_http_client_set_header(hc, "Content-Type", "application/json");
        esp_http_client_set_post_field(hc, tip_body, strlen(tip_body));
        esp_http_client_perform(hc);
        /* Response is short — read via event handler in production */
        esp_http_client_cleanup(hc);
        /* For brevity: from_block stays 0 — indexer will scan from genesis.
         * In production, wire up the event handler to capture the response. */
    }

    ESP_LOGI(TAG, "Polling CKB indexer for payment (from block %llu)",
             (unsigned long long)from_block);

    while ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(5000));  /* poll every 5s — ~1 CKB block */

        /* Call CKB indexer get_cells for merchant address
         * Full implementation mirrors CKBPaymentProvider::_checkIncomingCells()
         * Re-implemented here in C (that method is C++ Arduino). */
        /* TODO: extract address→lockscript conversion to a shared C util
         * (ckb_address.h) so both CKBPaymentProvider and this file share code */
        ESP_LOGD(TAG, "indexer poll tick");

        /* For now: check tip advancement as a liveness signal,
         * full cell scan via ckb_address util (tracked in TODO above). */
        /* When ckb_address.h lands, replace this block with:
         *   if (ckb_check_incoming_cells(s_cfg.ckb_rpc_url,
         *                                s_cfg.merchant_address,
         *                                amount_shannons,
         *                                from_block, &from_block))
         *       goto paid;
         */
    }

    ESP_LOGE(TAG, "L1 payment timeout");
    return WY_FIBER_ERR_TIMEOUT;

/* paid: */
    result->amount_shannons = amount_shannons;
    result->fiber = false;
    ESP_LOGI(TAG, "L1 payment confirmed — payer=%s", result->payer_address);
    return ESP_OK;
}


esp_err_t wa_ap_pay_to_credential(const char          *payee_cred_json,
                                   const char          *invoice,
                                   wy_payment_result_t *result)
{
    if (!s_initialised || !payee_cred_json || !invoice || !result)
        return ESP_ERR_INVALID_ARG;

    /* Verify the payee's identity before sending funds */
    char payee_addr[128] = {0};
    if (!_extract_address(payee_cred_json, payee_addr, sizeof(payee_addr))) {
        ESP_LOGE(TAG, "Could not extract payee address");
        return ESP_ERR_INVALID_ARG;
    }
    if (!wy_auth_verify_shim(payee_cred_json)) {
        ESP_LOGE(TAG, "Payee P-256 verify failed — refusing to send");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t timeout = s_cfg.payment_timeout_ms
                     ? s_cfg.payment_timeout_ms : DEFAULT_TIMEOUT_MS;

    ESP_LOGI(TAG, "Paying verified payee %s", payee_addr);
    return wy_fiber_send_payment(s_cfg.fiber_rpc_url, invoice, timeout, result);
}
