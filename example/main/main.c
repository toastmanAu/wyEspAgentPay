/**
 * wyEspAgentPay ESP32-P4 Example with C6 WiFi Coprocessor
 * =========================================================
 * Demonstrates Fiber invoice decoder + payment flow using
 * ESP32-P4 host with ESP32-C6 WiFi coprocessor over SDIO.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wy_agentpay.h"

static const char *TAG = "wyAgentPay_P4";

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", CONFIG_EXAMPLE_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", CONFIG_EXAMPLE_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void agentpay_demo_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting wyAgentPay demo...");
    
    // Configure wyAgentPay
    wy_agentpay_config_t config = {
        .fiber_rpc_url      = CONFIG_EXAMPLE_FIBER_RPC_URL,
        .payment_timeout_ms = 30000,
    };
    strncpy(config.node_description, "ESP32-P4 Demo", sizeof(config.node_description)-1);
    
    esp_err_t err = wy_agentpay_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AgentPay init failed: 0x%x", err);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "AgentPay initialized ✓");
    ESP_LOGI(TAG, "Calling: %s", CONFIG_EXAMPLE_TARGET_URL);
    
    // Make HTTP request (wyAgentPay handles 402 automatically)
    char resp_buf[2048];
    int http_status;
    err = wy_agentpay_call(CONFIG_EXAMPLE_TARGET_URL, "GET", NULL, 
                           resp_buf, sizeof(resp_buf), &http_status);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ HTTP %d — Payment flow complete!", http_status);
        ESP_LOGI(TAG, "Response: %.100s%s", resp_buf, strlen(resp_buf) > 100 ? "..." : "");
    } else {
        ESP_LOGE(TAG, "Request failed: 0x%x", err);
    }
    
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Demo complete. Restarting in 10s...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  wyEspAgentPay — ESP32-P4 + C6 WiFi Demo        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════╝");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Board: ESP32-P4 @ 400MHz (RISC-V)");
    ESP_LOGI(TAG, "WiFi: ESP32-C6 coprocessor (SDIO)");
    ESP_LOGI(TAG, "Libraries: wy_fiber_rpc, wy_agentpay, wy_x402");
    ESP_LOGI(TAG, "");
    
    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_init_sta();
    
    // Start payment demo
    xTaskCreate(agentpay_demo_task, "agentpay_demo", 8192, NULL, 5, NULL);
}
