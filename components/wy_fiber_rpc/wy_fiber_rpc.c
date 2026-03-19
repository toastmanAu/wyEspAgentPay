/*
 * wy_fiber_rpc.c — Fiber fnn JSON-RPC 2.0 client
 * ================================================
 * Implements wy_fiber_rpc.h against the real fnn API.
 * RPC signatures verified against fnn v0.7.1 on testnet.
 *
 * send_payment  params: { invoice: "fibt1..." }
 *               result: { payment_hash: "0x...", ... }
 *
 * get_payment   params: { payment_hash: "0x..." }
 *               result: { status: "Success"|"Inflight"|"Failed",
 *                         preimage: "0x..." (on Success) }
 *
 * new_invoice   params: { amount: <shannons>, description: "..." }
 *               result: { invoice_address: "fibt1...",
 *                         payment_hash: "0x..." }
 *
 * node_info     params: []
 *               result: { node_id: "0x...", ... }
 */
#include "wy_fiber_rpc.h"

#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wy_fiber_rpc";

#define RPC_BUF_SIZE    2048
#define POLL_INTERVAL   500   /* ms between get_payment polls */

/* ── Internal HTTP POST helper ────────────────────────────────── */

typedef struct {
    char   *buf;
    int     len;
    int     cap;
} _resp_buf_t;

static esp_err_t _http_event(esp_http_client_event_t *evt)
{
    _resp_buf_t *rb = (_resp_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rb) {
        int to_copy = evt->data_len;
        if (rb->len + to_copy >= rb->cap - 1) {
            to_copy = rb->cap - rb->len - 1;
        }
        if (to_copy > 0) {
            memcpy(rb->buf + rb->len, evt->data, to_copy);
            rb->len += to_copy;
            rb->buf[rb->len] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t _rpc_post(const char *url, const char *body,
                            char *resp_buf, size_t resp_cap)
{
    _resp_buf_t rb = { .buf = resp_buf, .len = 0, .cap = (int)resp_cap };
    resp_buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = 10000,
        .event_handler  = _http_event,
        .user_data      = &rb,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGE(TAG, "HTTP %d from %s", status, url);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

/* ── send_payment ─────────────────────────────────────────────── */

esp_err_t wy_fiber_send_payment(const char *rpc_url,
                                const char *invoice,
                                char       *payment_hash,
                                size_t      hash_len)
{
    char body[640];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"method\":\"send_payment\","
        "\"params\":[{\"invoice\":\"%s\"}],\"id\":1}", invoice);

    char resp[RPC_BUF_SIZE];
    esp_err_t err = _rpc_post(rpc_url, body, resp, sizeof(resp));
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    /* Check for RPC error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        ESP_LOGE(TAG, "send_payment RPC error: %s",
                 cJSON_IsString(msg) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *hash   = cJSON_GetObjectItem(result, "payment_hash");
    if (!cJSON_IsString(hash)) {
        ESP_LOGE(TAG, "send_payment: no payment_hash in result");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(payment_hash, hash->valuestring, hash_len - 1);
    payment_hash[hash_len - 1] = '\0';

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Payment inflight: %s", payment_hash);
    return ESP_OK;
}

/* ── get_payment ──────────────────────────────────────────────── */

esp_err_t wy_fiber_get_payment(const char          *rpc_url,
                               const char          *payment_hash,
                               wy_payment_status_t *status_out,
                               char                *preimage_out,
                               size_t               preimage_len)
{
    char body[256];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_payment\","
        "\"params\":[{\"payment_hash\":\"%s\"}],\"id\":1}", payment_hash);

    char resp[RPC_BUF_SIZE];
    esp_err_t err = _rpc_post(rpc_url, body, resp, sizeof(resp));
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON_Delete(root);
        *status_out = WY_PAYMENT_UNKNOWN;
        return ESP_FAIL;
    }

    cJSON *result  = cJSON_GetObjectItem(root, "result");
    cJSON *status  = cJSON_GetObjectItem(result, "status");

    if (!cJSON_IsString(status)) {
        cJSON_Delete(root);
        *status_out = WY_PAYMENT_UNKNOWN;
        return ESP_FAIL;
    }

    const char *s = status->valuestring;
    if (strcmp(s, "Success") == 0) {
        *status_out = WY_PAYMENT_SUCCESS;
        /* Extract preimage if buffer provided */
        if (preimage_out && preimage_len > 0) {
            cJSON *pre = cJSON_GetObjectItem(result, "preimage");
            if (cJSON_IsString(pre)) {
                strncpy(preimage_out, pre->valuestring, preimage_len - 1);
                preimage_out[preimage_len - 1] = '\0';
            } else {
                preimage_out[0] = '\0';
            }
        }
    } else if (strcmp(s, "Inflight") == 0) {
        *status_out = WY_PAYMENT_INFLIGHT;
    } else {
        *status_out = WY_PAYMENT_FAILED;
        ESP_LOGW(TAG, "Payment failed: %s", payment_hash);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── pay_and_wait ─────────────────────────────────────────────── */

esp_err_t wy_fiber_pay_and_wait(const char *rpc_url,
                                const char *invoice,
                                char       *preimage_out,
                                size_t      preimage_len,
                                uint32_t    timeout_ms)
{
    char payment_hash[70] = {0};

    esp_err_t err = wy_fiber_send_payment(rpc_url, invoice,
                                          payment_hash, sizeof(payment_hash));
    if (err != ESP_OK) return err;

    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL));
        elapsed += POLL_INTERVAL;

        wy_payment_status_t status;
        char preimage[70] = {0};

        err = wy_fiber_get_payment(rpc_url, payment_hash,
                                   &status, preimage, sizeof(preimage));
        if (err != ESP_OK) continue;  /* transient RPC error — keep polling */

        if (status == WY_PAYMENT_SUCCESS) {
            ESP_LOGI(TAG, "Payment settled in %ums", (unsigned)elapsed);
            if (preimage_out && preimage_len > 0) {
                strncpy(preimage_out, preimage, preimage_len - 1);
                preimage_out[preimage_len - 1] = '\0';
            }
            return ESP_OK;
        }
        if (status == WY_PAYMENT_FAILED) {
            ESP_LOGE(TAG, "Payment failed after %ums", (unsigned)elapsed);
            return ESP_FAIL;
        }
        /* WY_PAYMENT_INFLIGHT — keep polling */
    }

    ESP_LOGE(TAG, "Payment timed out after %ums", (unsigned)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

/* ── new_invoice ──────────────────────────────────────────────── */

esp_err_t wy_fiber_new_invoice(const char *rpc_url,
                               uint64_t    amount_shannons,
                               const char *description,
                               char       *invoice_out,
                               size_t      invoice_len,
                               char       *payment_hash_out,
                               size_t      hash_len)
{
    char body[512];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"method\":\"new_invoice\","
        "\"params\":[{\"amount\":%llu,\"description\":\"%s\"}],\"id\":1}",
        (unsigned long long)amount_shannons, description);

    char resp[RPC_BUF_SIZE];
    esp_err_t err = _rpc_post(rpc_url, body, resp, sizeof(resp));
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        ESP_LOGE(TAG, "new_invoice error: %s",
                 cJSON_IsString(msg) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *result  = cJSON_GetObjectItem(root, "result");
    cJSON *invoice = cJSON_GetObjectItem(result, "invoice_address");
    cJSON *hash    = cJSON_GetObjectItem(result, "payment_hash");

    if (!cJSON_IsString(invoice)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(invoice_out, invoice->valuestring, invoice_len - 1);
    invoice_out[invoice_len - 1] = '\0';

    if (payment_hash_out && hash_len > 0 && cJSON_IsString(hash)) {
        strncpy(payment_hash_out, hash->valuestring, hash_len - 1);
        payment_hash_out[hash_len - 1] = '\0';
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── node_info ────────────────────────────────────────────────── */

esp_err_t wy_fiber_node_info(const char *rpc_url,
                             char       *node_id_out,
                             size_t      node_id_len)
{
    const char *body =
        "{\"jsonrpc\":\"2.0\",\"method\":\"node_info\","
        "\"params\":[],\"id\":1}";

    char resp[RPC_BUF_SIZE];
    esp_err_t err = _rpc_post(rpc_url, body, resp, sizeof(resp));
    if (err != ESP_OK) return err;

    cJSON *root   = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    cJSON *result  = cJSON_GetObjectItem(root, "result");
    cJSON *node_id = cJSON_GetObjectItem(result, "node_id");

    if (cJSON_IsString(node_id) && node_id_out && node_id_len > 0) {
        strncpy(node_id_out, node_id->valuestring, node_id_len - 1);
        node_id_out[node_id_len - 1] = '\0';
    }

    cJSON_Delete(root);
    return (result != NULL) ? ESP_OK : ESP_FAIL;
}
