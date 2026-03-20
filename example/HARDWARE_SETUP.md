# Hardware Setup — ESP32-P4 + C6 SDIO Connection

## Board: Guition JC4880P433

Your board has **built-in P4↔C6 communication** via SDIO. No external wiring needed!

## SDIO Connection (Internal PCB Traces)

The ESP32-P4 and ESP32-C6 are connected via SDIO Slot 1 (fixed IOMUX pins):

| Signal | P4 GPIO | C6 GPIO | Notes |
|--------|---------|---------|-------|
| CLK    | GPIO43  | GPIO19  | SDIO clock |
| CMD    | GPIO44  | GPIO18  | SDIO command |
| D0     | GPIO39  | GPIO20  | Data line 0 |
| D1     | GPIO40  | GPIO21  | Data line 1 |
| D2     | GPIO41  | GPIO22  | Data line 2 (4-bit mode) |
| D3     | GPIO42  | GPIO23  | Data line 3 (4-bit mode) |

**Mode**: 4-bit SDIO (faster than 1-bit)

## Software Configuration

The P4 firmware (host) is already configured to use these pins via `SDMMC_SLOT_CONFIG_DEFAULT()` which auto-detects Slot 1 IOMUX.

The C6 firmware (slave) must be flashed with ESP-Hosted SDIO slave software that listens on the matching C6 GPIOs.

## Pin Access

These GPIOs are **not exposed on headers** — they're dedicated to the P4↔C6 internal link. You can't use them for other purposes.

## Troubleshooting

If WiFi initialization fails with "SDIO slave not responding":
1. C6 firmware not flashed or wrong transport (SPI instead of SDIO)
2. C6 in wrong boot mode (check BOOT/EN button sequence)
3. Hardware issue (rare on pre-assembled boards)

## Verification

Once C6 is flashed with ESP-Hosted SDIO slave firmware, you should see in P4 serial output:
```
I (xxx) esp_hosted: Slave init done
I (xxx) WIFI: WiFi started
```

No output → C6 firmware missing or misconfigured.
