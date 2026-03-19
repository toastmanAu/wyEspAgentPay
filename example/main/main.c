/*
 * example/main/main.c — wyEspAgentPay demo
 * =========================================
 * Connects to WiFi, checks fnn connectivity, then demonstrates:
 *   1. Payer: GET a 402-protected endpoint (auto-pays)
 *   2. Provider: generate a 402 response with a live invoice
 *
 * All config via menuconfig (idf.py menuconfig → wyEspAgentPay)
 * or at runtime via NVS — no hardcoded credentials.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wy_agentpay.h"

static const char *TAG = "agentpay_demo";

/* ── WiFi credentials: set via NVS or sdkconfig.defaults ─────── */
/* DO NOT hardcode credentials here. Use:
 *   idf.py menuconfig → Example Connection Configuration
 *   or provision via NVS at first boot.
 */
#ifndef CONFIG_EXAMPLE_WIFI_SSID
  #define CONFIG_EXAMPLE_WIFI_SSID "your_ssid"
#endif
#ifndef CONFIG_EXAMPLE_WIFI_PASSWORD
  #define CONFIG_EXAMPLE_WIFI_PASSWORD "your_password"
#endif

/* ── Demo task ────────────────────────────────────────────────── */
static void agentpay_demo_task(void *pvParameters)
{
    /* Wait for DHCP */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* ── Init AgentPay ──────────────────────────────────────── */
    wy_agentpay_config_t cfg = {
        .fiber_rpc_url      = CONFIG_WY_FIBER_RPC_URL,
        .payment_timeout_ms = CONFIG_WY_PAYMENT_TIMEOUT_MS,
        .http_timeout_ms    = CONFIG_WY_HTTP_TIMEOUT_MS,
    };

    esp_err_t err = wy_agentpay_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AgentPay init failed: %s", esp_err_to_name(err));
        goto done;
    }

    /* ── Demo 1: Payer — auto-pay a 402-protected GET ─────────── */
    /* Replace with your actual 402-protected service URL */
    const char *service_url = "http://192.168.68.84:8080/api/data";

    char response[1024] = {0};
    ESP_LOGI(TAG, "Requesting: %s", service_url);

    err = wy_agentpay_get(service_url, response, sizeof(response));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Response: %s", response);
    } else {
        ESP_LOGW(TAG, "Request failed (service may not be running): %s",
                 esp_err_to_name(err));
    }

    /* ── Demo 2: Provider — generate a 402 body ───────────────── */
    char invoice_body[600] = {0};
    err = wy_agentpay_make_402(
        1000,          /* 1000 shannons = 0.00001 CKB */
        "Demo API access",
        invoice_body,
        sizeof(invoice_body)
    );
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "402 body (send this to client):\n%s", invoice_body);
    } else {
        ESP_LOGW(TAG, "make_402 failed (fnn may be unreachable): %s",
                 esp_err_to_name(err));
    }

done:
    ESP_LOGI(TAG, "Demo complete.");
    vTaskDelete(NULL);
}

/* ── WiFi boilerplate ─────────────────────────────────────────── */
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    xTaskCreate(agentpay_demo_task, "agentpay", 8192, NULL, 5, NULL);
}
