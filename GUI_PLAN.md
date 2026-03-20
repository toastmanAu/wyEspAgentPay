# wyEspAgentPay GUI Plan

## Display Hardware
**Guition JC4880P433 Specs:**
- Panel: 4.3" MIPI-DSI 480×800 (portrait)
- Driver: ILI9488 or compatible
- Touch: Capacitive (I2C, likely GT911 or FT6236)
- Interface: ESP32-P4 MIPI-DSI port

## GUI Library: LVGL
**Why LVGL:**
- Official ESP-IDF support via `esp_lvgl_port` component
- Hardware-accelerated on ESP32-P4 (PPA engine)
- Touch-friendly widgets
- Small memory footprint
- Active development, good docs

## Screens Design

### 1. Boot/Status Screen
```
╔═══════════════════════════════╗
║                               ║
║      [Wyltek Logo]            ║
║                               ║
║   wyEspAgentPay v0.1.0        ║
║                               ║
║   WiFi: Connecting...         ║
║   Fiber: Checking node...     ║
║                               ║
║   [Progress spinner]          ║
║                               ║
╚═══════════════════════════════╝
```

### 2. Main/Idle Screen
```
╔═══════════════════════════════╗
║  12:34 PM          [WiFi ●]   ║
╠═══════════════════════════════╣
║                               ║
║   Ready to Pay                ║
║                               ║
║   Balance: 123.45 CKB         ║
║   Node: Connected ●           ║
║                               ║
║                               ║
║   [Scan QR Code]              ║
║                               ║
║   [Manual URL]                ║
║                               ║
╚═══════════════════════════════╝
```

### 3. Payment Request Screen (402 received)
```
╔═══════════════════════════════╗
║  Payment Required             ║
╠═══════════════════════════════╣
║                               ║
║   Provider: httpbin.org       ║
║                               ║
║   Amount: 0.001 CKB           ║
║            (~$0.0001 USD)     ║
║                               ║
║   Description:                ║
║   "Test payment endpoint"     ║
║                               ║
║                               ║
║   [✓ Pay Now]    [✗ Cancel]  ║
║                               ║
╚═══════════════════════════════╝
```

### 4. Processing Screen
```
╔═══════════════════════════════╗
║  Processing Payment...        ║
╠═══════════════════════════════╣
║                               ║
║   [Animated spinner]          ║
║                               ║
║   Sending to Fiber node...    ║
║                               ║
║   Do not close app            ║
║                               ║
╚═══════════════════════════════╝
```

### 5. Success Screen
```
╔═══════════════════════════════╗
║  ✓ Payment Sent!              ║
╠═══════════════════════════════╣
║                               ║
║   [Green checkmark]           ║
║                               ║
║   Amount: 0.001 CKB           ║
║   TX: 0x1234...5678           ║
║                               ║
║   Resource downloading...     ║
║                               ║
║   [View Receipt]  [Done]      ║
║                               ║
╚═══════════════════════════════╝
```

### 6. Error Screen
```
╔═══════════════════════════════╗
║  ⚠ Payment Failed             ║
╠═══════════════════════════════╣
║                               ║
║   [Red X icon]                ║
║                               ║
║   Error: Insufficient funds   ║
║                               ║
║   Your balance: 0.0005 CKB    ║
║   Required: 0.001 CKB         ║
║                               ║
║   [Retry]  [Cancel]           ║
║                               ║
╚═══════════════════════════════╝
```

## Implementation Steps

### Phase 1: Display Bringup (1-2 hours)
1. Add `esp_lvgl_port` managed component
2. Configure MIPI-DSI in sdkconfig (pins from datasheet)
3. Initialize display in main.c
4. Draw "Hello World" + FPS counter

### Phase 2: Touch Support (30 min)
1. Identify touch controller (GT911/FT6236)
2. Add I2C driver init
3. Register touch callbacks with LVGL
4. Test tap/swipe gestures

### Phase 3: Basic Screens (1 hour)
1. Boot screen (logo + status)
2. Main idle screen (balance + buttons)
3. Simple navigation (button → screen transition)

### Phase 4: Payment Flow (1-2 hours)
1. Wire up wy_agentpay callbacks to GUI events
2. Payment request screen (parse invoice, show amount)
3. Confirmation flow (approve/deny buttons)
4. Progress + result screens

### Phase 5: Polish (optional)
1. Animations (fade in/out, slide)
2. Better icons/graphics
3. QR code scanner (if camera present)
4. Settings screen (WiFi config, node URL)

## Resource Requirements

**Memory:**
- LVGL base: ~100 KB RAM
- Screen buffers: 480×800×2 = 768 KB (16-bit color, double buffer)
- ESP32-P4 has 512 KB internal + 32 MB PSRAM on this board
- Allocation: buffers in PSRAM, LVGL objects in internal RAM

**Flash:**
- LVGL library: ~200 KB
- Font assets: ~50 KB (default fonts)
- Current firmware: 898 KB → will be ~1.1 MB with GUI

**Performance:**
- Target: 30 FPS smooth UI
- P4 @ 400 MHz + PPA hardware acceleration should handle this easily

## Code Structure

```
example/
├── main/
│   ├── main.c              (entry point, WiFi, AgentPay init)
│   ├── gui/
│   │   ├── gui_init.c      (LVGL + display setup)
│   │   ├── gui_screens.c   (screen definitions)
│   │   ├── gui_events.c    (button callbacks)
│   │   └── gui_theme.c     (colors, fonts, styles)
│   └── ...
└── ...
```

## Next Session Plan

1. Check Guition datasheet for exact MIPI-DSI pinout
2. Add `espressif/esp_lvgl_port` component
3. Configure display in sdkconfig
4. Draw first screen: "wyEspAgentPay" title + WiFi status
5. If time: add payment button → test tap response

**Estimated total time: 3-5 hours for full payment UI**

Want to start this in the next session once WiFi is confirmed working?
