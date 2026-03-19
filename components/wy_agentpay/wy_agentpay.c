/*
 * wy_agentpay.c — Top-level AgentPay implementation
 */
#include "wy_agentpay.h"
#include "wy_fiber_rpc.h"
#include "wy_x402.h"

#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "wy_agentpay";

static wy_agentpay_config_t _cfg;
static bool _initialised = false;

/* ── Internal response collector ─────────────────────────────── */

typedef struct {
    char   *buf;
    int     len;
    int     cap;
} _resp_t;

static esp_err_t _collect_event(esp_http_client_event_t *evt)
{
    _resp_t *r = (_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && r) {
        int n = evt->data_len;
        if (r->len + n >= r->cap - 1) n = r->cap - r->len - 1;
        if (n > 0) {
            memcpy(r->buf + r->len, evt->data, n);
            r->len += n;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

/* ── Init ─────────────────────────────────────────────────────── */

esp_err_t wy_agentpay_init(const wy_agentpay_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memcpy(&_cfg, config, sizeof(_cfg));

    /* Apply defaults */
    if (_cfg.payment_timeout_ms == 0) _cfg.payment_timeout_ms = 30000;
    if (_cfg.http_timeout_ms    == 0) _cfg.http_timeout_ms    = 10000;

    /* Verify fnn connectivity */
    char node_id[70] = {0};
    esp_err_t err = wy_fiber_node_info(_cfg.fiber_rpc_url, node_id, sizeof(node_id));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "AgentPay ready. Fiber node: %.20s...", node_id);
    } else {
        ESP_LOGW(TAG, "AgentPay init: fnn unreachable at %s (payments will fail)",
                 _cfg.fiber_rpc_url);
        /* Non-fatal — WiFi may not be up yet */
    }

    _initialised = true;
    return ESP_OK;
}

/* ── Internal: perform request, handle 402 if needed ─────────── */

static esp_err_t _do_request(const char *url,
                             esp_http_client_method_t method,
                             const char *body,
                             char       *response_buf,
                             size_t      buf_len)
{
    if (!_initialised) {
        ESP_LOGE(TAG, "wy_agentpay_init() not called");
        return ESP_ERR_INVALID_STATE;
    }

    _resp_t resp = { .buf = response_buf, .len = 0, .cap = (int)buf_len };
    response_buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = method,
        .timeout_ms    = (int)_cfg.http_timeout_ms,
        .event_handler = _collect_event,
        .user_data     = &resp,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    if (body && method == HTTP_METHOD_POST) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) goto done;

    int status = esp_http_client_get_status_code(client);

    /* ── 402: pay and retry ───────────────────────────────────── */
    if (status == 402) {
        ESP_LOGI(TAG, "Got 402 from %s — initiating payment", url);

        char proof[140] = {0};
        err = wy_x402_handle(client, _cfg.fiber_rpc_url,
                             proof, sizeof(proof),
                             _cfg.payment_timeout_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Payment failed: %s", esp_err_to_name(err));
            goto done;
        }

        /* Reinitialise client for retry with proof header */
        esp_http_client_cleanup(client);
        resp.len = 0;
        response_buf[0] = '\0';

        client = esp_http_client_init(&cfg);
        if (!client) { err = ESP_ERR_NO_MEM; goto done_no_cleanup; }

        esp_http_client_set_header(client, "X-Payment-Proof", proof);

        if (body && method == HTTP_METHOD_POST) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, body, (int)strlen(body));
        }

        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            status = esp_http_client_get_status_code(client);
            if (status >= 400) {
                ESP_LOGE(TAG, "Retry after payment got HTTP %d", status);
                err = ESP_FAIL;
            }
        }
    } else if (status >= 400) {
        ESP_LOGE(TAG, "HTTP %d from %s", status, url);
        err = ESP_FAIL;
    }

done:
    esp_http_client_cleanup(client);
done_no_cleanup:
    return err;
}

/* ── Public API ───────────────────────────────────────────────── */

esp_err_t wy_agentpay_get(const char *url, char *response_buf, size_t buf_len)
{
    return _do_request(url, HTTP_METHOD_GET, NULL, response_buf, buf_len);
}

esp_err_t wy_agentpay_post(const char *url, const char *json_body,
                           char *response_buf, size_t buf_len)
{
    return _do_request(url, HTTP_METHOD_POST, json_body, response_buf, buf_len);
}

/* ── Provider: build 402 body ─────────────────────────────────── */

esp_err_t wy_agentpay_make_402(uint64_t    amount_shannons,
                               const char *description,
                               char       *out_json,
                               size_t      out_len)
{
    /* Must have a live fnn to generate a real invoice */
    char invoice[520]   = {0};
    char pay_hash[70]   = {0};

    esp_err_t err = wy_fiber_new_invoice(_cfg.fiber_rpc_url,
                                         amount_shannons, description,
                                         invoice, sizeof(invoice),
                                         pay_hash, sizeof(pay_hash));
    if (err != ESP_OK) return err;

    int n = snprintf(out_json, out_len,
        "{\"x402Version\":1,\"payment\":{"
        "\"scheme\":\"fiber\","
        "\"fiber_invoice\":\"%s\","
        "\"payment_hash\":\"%s\","
        "\"amount_shannons\":%llu,"
        "\"description\":\"%s\"}}",
        invoice, pay_hash,
        (unsigned long long)amount_shannons, description);

    return (n > 0 && (size_t)n < out_len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

/* ── Provider: verify proof header ───────────────────────────── */

esp_err_t wy_agentpay_verify_proof(const char *proof_header,
                                   const char *expected_hash)
{
    /*
     * Proof format: "preimage:payment_hash"
     * Verification: sha256(preimage) == payment_hash (without 0x prefix)
     *
     * Full cryptographic verification requires mbedTLS SHA-256.
     * For now: check that payment_hash portion matches expected_hash.
     * TODO: add mbedtls_sha256 verification of preimage.
     */
    if (!proof_header || !expected_hash) return ESP_ERR_INVALID_ARG;

    const char *colon = strchr(proof_header, ':');
    if (!colon) {
        ESP_LOGW(TAG, "Invalid proof format (no colon): %s", proof_header);
        return ESP_FAIL;
    }

    const char *hash_part = colon + 1;

    /* Strip leading 0x if present in expected */
    const char *exp = expected_hash;
    if (strncmp(exp, "0x", 2) == 0) exp += 2;
    const char *got = hash_part;
    if (strncmp(got, "0x", 2) == 0) got += 2;

    if (strcasecmp(exp, got) == 0) {
        ESP_LOGI(TAG, "Proof verified for hash %s", expected_hash);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Proof hash mismatch: expected %s got %s", exp, got);
    return ESP_FAIL;
}
