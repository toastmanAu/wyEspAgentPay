/**
 * wy_agentpay.c — Top-level AgentPay client + server implementation
 */

#include "wy_agentpay.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wy_agentpay";

static wy_agentpay_config_t s_cfg = {0};
static bool s_initialised = false;

/* ── Payer API ─────────────────────────────────────────────────── */

esp_err_t wy_agentpay_init(const wy_agentpay_config_t *cfg)
{
    if (!cfg || strlen(cfg->fiber_rpc_url) == 0) return ESP_ERR_INVALID_ARG;

    memcpy(&s_cfg, cfg, sizeof(wy_agentpay_config_t));

    /* Verify fnn connectivity */
    wy_node_info_t info = {0};
    esp_err_t err = wy_fiber_node_info(s_cfg.fiber_rpc_url, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot reach fnn at %s (err 0x%x)", s_cfg.fiber_rpc_url, err);
        return err;
    }

    ESP_LOGI(TAG, "Connected to fnn %s | node %.16s... | channels: %lu",
             info.version, info.node_id, (unsigned long)info.channel_count);

    s_initialised = true;
    return ESP_OK;
}

esp_err_t wy_agentpay_call(const char *url,
                            const char *method,
                            const char *post_body,
                            char       *resp_buf,
                            size_t      resp_len,
                            int        *http_status)
{
    if (!s_initialised) {
        ESP_LOGE(TAG, "wy_agentpay_init() not called");
        return ESP_ERR_INVALID_STATE;
    }

    wy_x402_config_t x402_cfg = {
        .fiber_rpc_url      = s_cfg.fiber_rpc_url,
        .payment_timeout_ms = s_cfg.payment_timeout_ms,
    };

    return wy_x402_perform(url, method, post_body, &x402_cfg,
                           resp_buf, resp_len, http_status, NULL);
}

/* ── Provider (server) API ─────────────────────────────────────── */

esp_err_t wy_agentpay_server_init(wy_agentpay_server_t       *srv,
                                   const wy_agentpay_config_t *cfg,
                                   uint64_t                    price_shannons)
{
    if (!srv || !cfg) return ESP_ERR_INVALID_ARG;
    memcpy(&srv->cfg, cfg, sizeof(wy_agentpay_config_t));
    srv->price_shannons = price_shannons;

    /* Verify fnn connectivity */
    wy_node_info_t info = {0};
    esp_err_t err = wy_fiber_node_info(srv->cfg.fiber_rpc_url, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Server: cannot reach fnn (err 0x%x)", err);
        return err;
    }
    ESP_LOGI(TAG, "Server ready — price: %llu shannons, fnn: %s",
             (unsigned long long)price_shannons, info.node_id);
    return ESP_OK;
}

esp_err_t wy_agentpay_server_make_challenge(wy_agentpay_server_t *srv,
                                             char                 *out_buf,
                                             size_t                out_len,
                                             char                 *hash_out)
{
    if (!srv || !out_buf) return ESP_ERR_INVALID_ARG;

    /* Create a hold invoice for the required amount */
    char invoice[512] = {0};
    esp_err_t err = wy_fiber_new_hold_invoice(
        srv->cfg.fiber_rpc_url,
        srv->price_shannons,
        srv->cfg.node_description[0] ? srv->cfg.node_description : "AgentPay request",
        invoice,
        sizeof(invoice)
    );
    if (err != ESP_OK) return err;

    /* Build 402 response JSON */
    cJSON *root    = cJSON_CreateObject();
    cJSON *payment = cJSON_CreateObject();
    cJSON_AddStringToObject(payment, "fiber_invoice",    invoice);
    char amt_str[24];
    snprintf(amt_str, sizeof(amt_str), "%llu", (unsigned long long)srv->price_shannons);
    cJSON_AddStringToObject(payment, "amount_shannons",  amt_str);
    cJSON_AddItemToObject(root, "payment", payment);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    if (strlen(json) >= out_len) {
        cJSON_free(json);
        return WY_FIBER_ERR_BUFFER;
    }
    strncpy(out_buf, json, out_len - 1);
    cJSON_free(json);

    /* Extract payment hash from invoice for later verification.
     * fnn invoices encode the hash — for now we return empty and
     * let verify_proof check status by querying fnn directly.
     * TODO: decode BOLT11 payment hash from invoice bytes. */
    if (hash_out) hash_out[0] = '\0';

    return ESP_OK;
}

esp_err_t wy_agentpay_server_verify_proof(wy_agentpay_server_t *srv,
                                           const char           *proof_header,
                                           const char           *expected_hash)
{
    if (!srv || !proof_header) return ESP_ERR_INVALID_ARG;

    /* Proof format: "<preimage_hex>:<payment_hash_hex>" */
    char buf[140] = {0};
    strncpy(buf, proof_header, sizeof(buf) - 1);

    char *colon = strchr(buf, ':');
    if (!colon) {
        ESP_LOGE(TAG, "Invalid proof header format");
        return ESP_ERR_INVALID_ARG;
    }
    *colon = '\0';
    const char *preimage     = buf;
    const char *payment_hash = colon + 1;

    /* Optionally verify expected hash matches */
    if (expected_hash && strlen(expected_hash) > 0) {
        if (strcmp(payment_hash, expected_hash) != 0) {
            ESP_LOGE(TAG, "Payment hash mismatch");
            return ESP_ERR_INVALID_ARG;
        }
    }

    /* Confirm with fnn that payment is settled */
    wy_payment_status_t status;
    char confirmed_preimage[66] = {0};
    esp_err_t err = wy_fiber_get_payment_status(
        srv->cfg.fiber_rpc_url,
        payment_hash,
        &status,
        confirmed_preimage,
        sizeof(confirmed_preimage)
    );
    if (err != ESP_OK) return err;

    if (status != WY_PAYMENT_SUCCESS) {
        ESP_LOGE(TAG, "Payment not settled (status %d)", status);
        return WY_FIBER_ERR_REJECTED;
    }

    /* Verify preimage matches what fnn recorded */
    if (strlen(confirmed_preimage) > 0 &&
        strcmp(confirmed_preimage, preimage) != 0) {
        ESP_LOGE(TAG, "Preimage mismatch — possible replay attack");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Payment verified: %.16s...", payment_hash);
    return ESP_OK;
}
