/**
 * wyEspAgentPay ESP32-P4 Example
 * ===============================
 * Tests the Fiber invoice decoder without WiFi.
 * 
 * For WiFi: ESP32-P4 uses C6 coprocessor via SDIO.
 * Flash AT firmware to C6, then use esp_at or esp_hosted driver.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wy_fiber_invoice.h"

static const char *TAG = "wyAgentPay_P4";

/**
 * Test: Decode a Fiber invoice (offline, no network)
 */
static void test_invoice_decode(void)
{
    ESP_LOGI(TAG, "=== Fiber Invoice Decoder Test ===");
    
    // Example testnet invoice (fibt1...) - replace with real one for testing
    // This is a placeholder - actual invoices are much longer
    const char *test_invoice = 
        "fibt1qqqsyqcyq5rqwzqfqqqsyqcyq5rqwzqfqqqsyqcyq5rqwzqfqypqhp58yjmdan79s6qqdhdzgynm4zwqd5d7xmw5fk98klysy043l2ahrqsnp4q0n326hr8v9zprg8gsvezcch06gfaqqhde2aj730yg0durunfhv66859qfhxgqypm";
    
    wy_fiber_invoice_t decoded;
    esp_err_t err = wy_fiber_invoice_decode(test_invoice, &decoded);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ Decode SUCCESS");
        ESP_LOGI(TAG, "  Currency: %s", 
                 decoded.currency == WY_FIBER_TESTNET ? "testnet" : 
                 decoded.currency == WY_FIBER_MAINNET ? "mainnet" : "devnet");
        ESP_LOGI(TAG, "  Amount: %llu shannons", (unsigned long long)decoded.amount_shannons);
        ESP_LOGI(TAG, "  Payment hash: %s", decoded.payment_hash_hex);
        ESP_LOGI(TAG, "  Description: %s", decoded.description);
        ESP_LOGI(TAG, "  Timestamp: %llu", (unsigned long long)decoded.timestamp);
        ESP_LOGI(TAG, "  Expiry: %llu seconds", (unsigned long long)decoded.expiry_seconds);
    } else {
        ESP_LOGE(TAG, "✗ Decode FAILED: 0x%x", err);
        if (err == WY_INV_ERR_PREFIX) ESP_LOGE(TAG, "  Unknown currency prefix");
        if (err == WY_INV_ERR_BECH32) ESP_LOGE(TAG, "  Bech32 decode error");
        if (err == WY_INV_ERR_DECOMPRESS) ESP_LOGE(TAG, "  Arithmetic decompress failed");
        if (err == WY_INV_ERR_PARSE) ESP_LOGE(TAG, "  Molecule parse error");
    }
}

/**
 * Test: Verify preimage (offline proof verification)
 */
static void test_preimage_verify(void)
{
    ESP_LOGI(TAG, "=== Preimage Verification Test ===");
    
    // Example: payment hash and preimage (both 32 bytes hex)
    // In real use: hash comes from invoice, preimage from payment proof header
    const char *test_preimage = "0000000000000000000000000000000000000000000000000000000000000000";
    
    // Create a mock invoice with known hash
    wy_fiber_invoice_t invoice = {0};
    // SHA256("0000...") for testing - in real use this comes from decoded invoice
    
    esp_err_t err = wy_fiber_verify_preimage(&invoice, test_preimage);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ Preimage valid");
    } else {
        ESP_LOGW(TAG, "✗ Preimage invalid (expected - mock data)");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  wyEspAgentPay — ESP32-P4 Test                   ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════╝");
    
    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Board: ESP32-P4 (400MHz RISC-V)");
    ESP_LOGI(TAG, "WiFi: C6 coprocessor (SDIO) — not configured yet");
    ESP_LOGI(TAG, "Libraries: wy_fiber_rpc, wy_agentpay, wy_x402");
    ESP_LOGI(TAG, "");
    
    // Test 1: Invoice decoder
    test_invoice_decode();
    ESP_LOGI(TAG, "");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 2: Preimage verification
    test_preimage_verify();
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "=== All tests complete ===");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Next steps:");
    ESP_LOGI(TAG, "1. Flash AT firmware to C6 coprocessor");
    ESP_LOGI(TAG, "2. Configure SDIO communication (P4 ↔ C6)");
    ESP_LOGI(TAG, "3. Enable wyAgentPay network features");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "See: https://github.com/espressif/esp-at");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
