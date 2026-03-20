/**
 * Simple WiFi scanner — lists all visible 2.4GHz networks
 * Temporary diagnostic tool
 */

#include "esp_wifi.h"
#include "esp_log.h"

static const char *TAG = "wifi_scan";

void wifi_scan_and_print(void)
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,  // Show hidden networks too
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: 0x%x", err);
        return;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    ESP_LOGI(TAG, "Found %d networks:", ap_count);
    
    if (ap_count > 0) {
        wifi_ap_record_t *ap_list = malloc(ap_count * sizeof(wifi_ap_record_t));
        if (ap_list) {
            esp_wifi_scan_get_ap_records(&ap_count, ap_list);
            
            for (int i = 0; i < ap_count; i++) {
                ESP_LOGI(TAG, "  [%d] SSID: %-32s | RSSI: %3d | Ch: %2d | Auth: %d",
                         i + 1,
                         ap_list[i].ssid,
                         ap_list[i].rssi,
                         ap_list[i].primary,
                         ap_list[i].authmode);
            }
            
            free(ap_list);
        }
    } else {
        ESP_LOGW(TAG, "No networks found! (ESP32 can only see 2.4GHz)");
    }
}
