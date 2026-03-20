# ESP32-C6 WiFi Coprocessor Firmware

Your Guition JC4880P433 board has a built-in ESP32-C6 that provides WiFi for the ESP32-P4 via SDIO.

## C6 Firmware Status

⚠️ **The C6 may not have ESP-Hosted firmware pre-flashed.** You need to flash it before WiFi will work.

## How to Flash C6 Firmware

The C6 coprocessor is a separate chip on your board. You'll flash it independently from the P4.

### Option 1: Pre-built Binary (Quickest)

1. Download the latest ESP-Hosted SDIO slave firmware for ESP32-C6:
   ```bash
   git clone --recursive https://github.com/espressif/esp-hosted
   cd esp-hosted/esp_hosted_ng/esp/esp_driver/network_adapter
   ```

2. Build for ESP32-C6 SDIO slave:
   ```bash
   idf.py set-target esp32c6
   idf.py menuconfig
   # Navigate to: Component config → ESP-Hosted config → Transport layer
   # Select: SDIO only
   idf.py build
   ```

3. Flash to C6 (you may need to put the board in C6 flash mode — check board docs):
   ```bash
   idf.py -p /dev/ttyACM0 flash
   ```

### Option 2: OTA Update (If Board Supports It)

Some dev boards allow OTA firmware updates to the C6 from the P4 side. Check your board documentation.

### Option 3: ESP-Prog (If You Have One)

If your board exposes C6 UART/JTAG pins separately, you can use an ESP-Prog to flash it directly.

## Verifying C6 Firmware

Once flashed, the P4 should be able to initialize WiFi. Check serial output for:
```
I (xxx) esp_hosted: Slave init done
I (xxx) WIFI: WiFi initialized
```

If you see "SDIO slave not responding", the C6 firmware isn't running or isn't configured for SDIO.

## Board-Specific Notes

**Guition JC4880P433:**
- C6 coprocessor is factory-installed
- Check if it comes pre-flashed with ESP-Hosted (test by flashing P4 firmware and checking WiFi)
- If not, you'll need to flash C6 separately
- Consult Guition docs for C6 flash procedure (may require jumper or button combo)

## Resources

- [ESP-Hosted GitHub](https://github.com/espressif/esp-hosted)
- [ESP-Hosted Setup Guide](https://github.com/espressif/esp-hosted/blob/master/esp_hosted_ng/docs/setup.md)
- [ESP32-P4 + External WiFi Blog](https://developer.espressif.com/blog/wireless-connectivity-solutions-for-esp32-p4/)
