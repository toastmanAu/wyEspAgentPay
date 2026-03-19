/**
 * example/main/main.c — wyEspAgentPay demo
 * =========================================
 * Demonstrates both payer and provider flows.
 *
 * Payer: connects to WiFi, calls a 402-protected URL, pays automatically.
 * Provider: not shown here — see wy_agentpay_server_* API in wy_agentpay.h.
 *
 * CONFIG (set via menuconfig or sdkconfig.defaults):
 *   CONFIG_EXAMPLE_WIFI_SSID
 *   CONFIG_EXAMPLE_WIFI_PASS
 *   CONFIG_EXAMPLE_FIBER_RPC_URL    e.g. "http://192.168.1.1:8227"
 *   CONFIG_EXAMPLE_TARGET_URL       e.g. "http://myservice.local/api/data"
 *
 * NO SECRETS ARE HARDCODED. All config is provided at build time via Kconfig.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "wy_agentpay.h"

static const char *TAG = "agentpay_demo";

/* ── WiFi helpers (minimal) ────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected — IP: " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,  wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wcfg = {
        .sta = {
            .ssid     = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_start();

    /* Wait for connection */
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/* ── Demo task ─────────────────────────────────────────────────── */
static void agentpay_demo_task(void *pvParam)
{
    /* 1. Init AgentPay — verifies fnn connectivity */
    wy_agentpay_config_t cfg = {
        .fiber_rpc_url      = CONFIG_EXAMPLE_FIBER_RPC_URL,
        .payment_timeout_ms = 30000,
        .node_description   = "wyEspAgentPay demo",
    };

    esp_err_t err = wy_agentpay_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AgentPay init failed: 0x%x — check fnn is running", err);
        vTaskDelete(NULL);
        return;
    }

    /* 2. Make a paid API call — handles 402 automatically */
    char response[2048] = {0};
    int  http_status    = 0;

    ESP_LOGI(TAG, "Calling: %s", CONFIG_EXAMPLE_TARGET_URL);
    err = wy_agentpay_call(CONFIG_EXAMPLE_TARGET_URL,
                            "GET", NULL,
                            response, sizeof(response),
                            &http_status);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Success (HTTP %d): %.200s", http_status, response);
    } else {
        ESP_LOGE(TAG, "Call failed: 0x%x", err);
    }

    vTaskDelete(NULL);
}

/* ── app_main ──────────────────────────────────────────────────── */
void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    xTaskCreate(agentpay_demo_task, "agentpay", 8192, NULL, 5, NULL);
}
