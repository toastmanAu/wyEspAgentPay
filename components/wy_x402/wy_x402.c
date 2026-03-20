/**
 * wy_x402.c — HTTP 402 intercept implementation
 */

#include "wy_x402.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON/cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wy_x402";

#define X402_BODY_BUF   1024
#define PROOF_HDR_LEN   140   /* "preimage_hex:payment_hash_hex\0" */

/* ── Internal HTTP helper ──────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} http_resp_t;

static esp_err_t _evt(esp_http_client_event_t *evt)
{
    http_resp_t *r = (http_resp_t *)evt->user_data;
    if (!r) return ESP_OK;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t copy = evt->data_len;
        if (r->len + copy >= r->cap) copy = r->cap - r->len - 1;
        if (copy > 0) {
            memcpy(r->buf + r->len, evt->data, copy);
            r->len += copy;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t _do_request(const char  *url,
                              const char  *method,
                              const char  *body,
                              const char  *proof_header, /* NULL if first attempt */
                              char        *resp_buf,
                              size_t       resp_len,
                              int         *status_out)
{
    http_resp_t resp = { .buf = resp_buf, .len = 0, .cap = resp_len };

    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = (strcasecmp(method, "POST") == 0) ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms    = 15000,
        .event_handler = _evt,
        .user_data     = &resp,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    if (proof_header) {
        /* X-Payment-Proof: <preimage_hex>:<payment_hash_hex> */
        esp_http_client_set_header(client, "X-Payment-Proof", proof_header);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && status_out) {
        *status_out = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return err;
}

/* ── Parse 402 body ────────────────────────────────────────────── */

static esp_err_t _parse_402(const char *body,
                             char       *invoice_out,
                             size_t      invoice_len)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) return ESP_FAIL;

    esp_err_t ret = ESP_FAIL;
    cJSON *payment = cJSON_GetObjectItem(root, "payment");
    if (!payment) goto done;

    cJSON *inv = cJSON_GetObjectItem(payment, "fiber_invoice");
    if (!inv || !cJSON_IsString(inv)) goto done;

    if (strlen(inv->valuestring) >= invoice_len) goto done;

    strncpy(invoice_out, inv->valuestring, invoice_len - 1);
    invoice_out[invoice_len - 1] = '\0';
    ret = ESP_OK;

done:
    cJSON_Delete(root);
    return ret;
}

/* ── Public API ────────────────────────────────────────────────── */

esp_err_t wy_x402_perform(const char              *url,
                           const char              *method,
                           const char              *post_body,
                           const wy_x402_config_t  *x402_cfg,
                           char                    *resp_buf,
                           size_t                   resp_len,
                           int                     *http_status,
                           wy_payment_result_t     *payment_out)
{
    int status = 0;

    /* First attempt */
    esp_err_t err = _do_request(url, method, post_body, NULL,
                                resp_buf, resp_len, &status);
    if (err != ESP_OK) return err;

    if (status != 402) {
        if (http_status) *http_status = status;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "402 received — parsing payment request");

    /* Parse invoice from 402 body */
    char invoice[512] = {0};
    err = _parse_402(resp_buf, invoice, sizeof(invoice));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse 402 body: %.120s", resp_buf);
        return err;
    }
    ESP_LOGI(TAG, "Invoice: %.40s...", invoice);

    /* Pay via Fiber */
    wy_payment_result_t pay_result = {0};
    err = wy_fiber_send_payment(x402_cfg->fiber_rpc_url,
                                invoice,
                                x402_cfg->payment_timeout_ms,
                                &pay_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Payment failed: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "Payment settled — hash: %.16s...", pay_result.payment_hash);

    if (payment_out) memcpy(payment_out, &pay_result, sizeof(wy_payment_result_t));

    /* Build proof header: "<preimage>:<payment_hash>" */
    char proof[PROOF_HDR_LEN] = {0};
    snprintf(proof, sizeof(proof), "%s:%s", pay_result.preimage, pay_result.payment_hash);

    /* Retry with proof */
    memset(resp_buf, 0, resp_len);
    err = _do_request(url, method, post_body, proof,
                      resp_buf, resp_len, &status);
    if (http_status) *http_status = status;

    return err;
}
