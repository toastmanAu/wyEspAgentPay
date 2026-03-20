/**
 * wy_fiber_rpc.c — Fiber Network JSON-RPC client implementation
 * =============================================================
 * All RPC calls tested against fnn v0.7.0.
 * Uses esp_http_client + cJSON. No task/thread creation.
 */

#include "wy_fiber_rpc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON/cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wy_fiber_rpc";

#define DEFAULT_PAYMENT_TIMEOUT_MS  30000
#define PAYMENT_POLL_INTERVAL_MS    1000
#define HTTP_TIMEOUT_MS             10000
#define RPC_RESP_BUF                4096

/* ── Internal HTTP helper ──────────────────────────────────────── */

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} rpc_resp_t;

static esp_err_t _http_event(esp_http_client_event_t *evt)
{
    rpc_resp_t *r = (rpc_resp_t *)evt->user_data;
    if (!r) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (r->len + evt->data_len < r->cap) {
            memcpy(r->buf + r->len, evt->data, evt->data_len);
            r->len += evt->data_len;
            r->buf[r->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * Post a JSON-RPC body to rpc_url, return parsed cJSON response.
 * Caller must cJSON_Delete() the result.
 * Returns NULL on transport or parse error.
 */
static cJSON *_rpc_call(const char *rpc_url, const char *body)
{
    char resp_buf[RPC_RESP_BUF] = {0};
    rpc_resp_t resp = { .buf = resp_buf, .len = 0, .cap = RPC_RESP_BUF - 1 };

    esp_http_client_config_t cfg = {
        .url         = rpc_url,
        .method      = HTTP_METHOD_POST,
        .timeout_ms  = HTTP_TIMEOUT_MS,
        .event_handler = _http_event,
        .user_data   = &resp,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return NULL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        return NULL;
    }

    cJSON *json = cJSON_Parse(resp_buf);
    if (!json) {
        ESP_LOGE(TAG, "JSON parse failed: %.120s", resp_buf);
    }
    return json;
}

/* Check for fnn error object; log and return WY_FIBER_ERR_RPC_ERROR if present */
static esp_err_t _check_rpc_error(cJSON *root)
{
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (err) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        ESP_LOGE(TAG, "RPC error: %s", msg ? msg->valuestring : "(no message)");
        return WY_FIBER_ERR_RPC_ERROR;
    }
    return ESP_OK;
}

/* ── node_info ─────────────────────────────────────────────────── */

esp_err_t wy_fiber_node_info(const char *rpc_url, wy_node_info_t *out)
{
    const char *body = "{\"jsonrpc\":\"2.0\",\"method\":\"node_info\",\"params\":[],\"id\":1}";
    cJSON *root = _rpc_call(rpc_url, body);
    if (!root) return WY_FIBER_ERR_RPC_FAIL;

    esp_err_t ret = _check_rpc_error(root);
    if (ret != ESP_OK) goto done;

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result) { ret = WY_FIBER_ERR_PARSE; goto done; }

    if (out) {
        cJSON *nid = cJSON_GetObjectItem(result, "node_id");
        cJSON *ver = cJSON_GetObjectItem(result, "version");
        cJSON *chans = cJSON_GetObjectItem(result, "channels");

        if (nid && cJSON_IsString(nid))
            strncpy(out->node_id, nid->valuestring, sizeof(out->node_id) - 1);
        if (ver && cJSON_IsString(ver))
            strncpy(out->version, ver->valuestring, sizeof(out->version) - 1);
        out->channel_count = chans ? (uint32_t)cJSON_GetArraySize(chans) : 0;
    }

done:
    cJSON_Delete(root);
    return ret;
}

/* ── send_payment ──────────────────────────────────────────────── */

esp_err_t wy_fiber_send_payment(const char          *rpc_url,
                                const char          *invoice,
                                uint32_t             timeout_ms,
                                wy_payment_result_t *result)
{
    if (timeout_ms == 0) timeout_ms = DEFAULT_PAYMENT_TIMEOUT_MS;

    /* Build: {"jsonrpc":"2.0","method":"send_payment","params":[{"invoice":"..."}],"id":1} */
    cJSON *req   = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON *p0    = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method",  "send_payment");
    cJSON_AddStringToObject(p0,  "invoice", invoice);
    cJSON_AddItemToArray(params, p0);
    cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return ESP_ERR_NO_MEM;

    cJSON *root = _rpc_call(rpc_url, body);
    cJSON_free(body);
    if (!root) return WY_FIBER_ERR_RPC_FAIL;

    esp_err_t ret = _check_rpc_error(root);
    if (ret != ESP_OK) goto done;

    /* fnn returns payment_hash immediately; payment may still be in-flight */
    cJSON *res = cJSON_GetObjectItem(root, "result");
    if (!res) { ret = WY_FIBER_ERR_PARSE; goto done; }

    char payment_hash[66] = {0};
    cJSON *ph = cJSON_GetObjectItem(res, "payment_hash");
    if (!ph || !cJSON_IsString(ph)) { ret = WY_FIBER_ERR_PARSE; goto done; }
    strncpy(payment_hash, ph->valuestring, sizeof(payment_hash) - 1);

    cJSON_Delete(root);
    root = NULL;

    /* Poll for settlement */
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(PAYMENT_POLL_INTERVAL_MS));
        elapsed += PAYMENT_POLL_INTERVAL_MS;

        char preimage_buf[66] = {0};
        wy_payment_status_t status;
        ret = wy_fiber_get_payment_status(rpc_url, payment_hash,
                                          &status, preimage_buf, sizeof(preimage_buf));
        if (ret != ESP_OK) continue;

        if (status == WY_PAYMENT_SUCCESS) {
            if (result) {
                strncpy(result->payment_hash, payment_hash, sizeof(result->payment_hash) - 1);
                strncpy(result->preimage,     preimage_buf,  sizeof(result->preimage) - 1);
                result->status = WY_PAYMENT_SUCCESS;
            }
            return ESP_OK;
        }
        if (status == WY_PAYMENT_FAILED) {
            if (result) result->status = WY_PAYMENT_FAILED;
            return WY_FIBER_ERR_REJECTED;
        }
    }

    if (result) result->status = WY_PAYMENT_UNKNOWN;
    return WY_FIBER_ERR_TIMEOUT;

done:
    if (root) cJSON_Delete(root);
    return ret;
}

/* ── get_payment_status ────────────────────────────────────────── */

esp_err_t wy_fiber_get_payment_status(const char          *rpc_url,
                                      const char          *payment_hash,
                                      wy_payment_status_t *status_out,
                                      char                *preimage_out,
                                      size_t               preimage_len)
{
    cJSON *req    = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON *p0     = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method",  "get_payment");
    cJSON_AddStringToObject(p0,  "payment_hash", payment_hash);
    cJSON_AddItemToArray(params, p0);
    cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return ESP_ERR_NO_MEM;

    cJSON *root = _rpc_call(rpc_url, body);
    cJSON_free(body);
    if (!root) return WY_FIBER_ERR_RPC_FAIL;

    esp_err_t ret = _check_rpc_error(root);
    if (ret != ESP_OK) goto done;

    cJSON *res = cJSON_GetObjectItem(root, "result");
    if (!res) { ret = WY_FIBER_ERR_PARSE; goto done; }

    /* fnn status field: "Created"|"InFlight"|"Success"|"Failed" */
    cJSON *status_j = cJSON_GetObjectItem(res, "status");
    if (status_j && cJSON_IsString(status_j)) {
        const char *s = status_j->valuestring;
        if      (strcmp(s, "Success") == 0) *status_out = WY_PAYMENT_SUCCESS;
        else if (strcmp(s, "Failed")  == 0) *status_out = WY_PAYMENT_FAILED;
        else                                *status_out = WY_PAYMENT_PENDING;
    } else {
        *status_out = WY_PAYMENT_UNKNOWN;
    }

    if (*status_out == WY_PAYMENT_SUCCESS && preimage_out) {
        cJSON *pre = cJSON_GetObjectItem(res, "preimage");
        if (pre && cJSON_IsString(pre)) {
            strncpy(preimage_out, pre->valuestring, preimage_len - 1);
            preimage_out[preimage_len - 1] = '\0';
        }
    }

done:
    cJSON_Delete(root);
    return ret;
}

/* ── new_hold_invoice ──────────────────────────────────────────── */

esp_err_t wy_fiber_new_hold_invoice(const char *rpc_url,
                                    uint64_t    amount_shannons,
                                    const char *description,
                                    char       *invoice_out,
                                    size_t      out_len)
{
    cJSON *req    = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON *p0     = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method",  "new_invoice");
    /* amount as string — fnn expects uint128 in some versions */
    char amount_str[24];
    snprintf(amount_str, sizeof(amount_str), "%llu", (unsigned long long)amount_shannons);
    cJSON_AddStringToObject(p0, "amount",      amount_str);
    cJSON_AddStringToObject(p0, "description", description ? description : "");
    cJSON_AddItemToArray(params, p0);
    cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return ESP_ERR_NO_MEM;

    cJSON *root = _rpc_call(rpc_url, body);
    cJSON_free(body);
    if (!root) return WY_FIBER_ERR_RPC_FAIL;

    esp_err_t ret = _check_rpc_error(root);
    if (ret != ESP_OK) goto done;

    cJSON *res     = cJSON_GetObjectItem(root, "result");
    cJSON *invoice = res ? cJSON_GetObjectItem(res, "invoice_address") : NULL;
    if (!invoice || !cJSON_IsString(invoice)) {
        ret = WY_FIBER_ERR_PARSE;
        goto done;
    }

    if (strlen(invoice->valuestring) >= out_len) {
        ret = WY_FIBER_ERR_BUFFER;
        goto done;
    }
    strncpy(invoice_out, invoice->valuestring, out_len - 1);
    invoice_out[out_len - 1] = '\0';

done:
    cJSON_Delete(root);
    return ret;
}

/* ── settle_hold / cancel_hold ─────────────────────────────────── */

esp_err_t wy_fiber_settle_hold(const char *rpc_url,
                               const char *payment_hash,
                               const char *preimage)
{
    cJSON *req    = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON *p0     = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method",  "settle_invoice");
    cJSON_AddStringToObject(p0,  "payment_hash", payment_hash);
    cJSON_AddStringToObject(p0,  "preimage",      preimage);
    cJSON_AddItemToArray(params, p0);
    cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return ESP_ERR_NO_MEM;

    cJSON *root = _rpc_call(rpc_url, body);
    cJSON_free(body);
    if (!root) return WY_FIBER_ERR_RPC_FAIL;

    esp_err_t ret = _check_rpc_error(root);
    cJSON_Delete(root);
    return ret;
}

esp_err_t wy_fiber_cancel_hold(const char *rpc_url, const char *payment_hash)
{
    cJSON *req    = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON *p0     = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method",  "cancel_invoice");
    cJSON_AddStringToObject(p0,  "payment_hash", payment_hash);
    cJSON_AddItemToArray(params, p0);
    cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return ESP_ERR_NO_MEM;

    cJSON *root = _rpc_call(rpc_url, body);
    cJSON_free(body);
    if (!root) return WY_FIBER_ERR_RPC_FAIL;

    esp_err_t ret = _check_rpc_error(root);
    cJSON_Delete(root);
    return ret;
}
