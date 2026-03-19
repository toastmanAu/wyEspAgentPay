# wyEspAgentPay

ESP-IDF component library implementing the **x402 micropayment protocol** over **Fiber Network (CKB Lightning)** for ESP32 devices.

Drop this library into any ESP-IDF project to enable automatic machine-to-machine payments: when an HTTP endpoint returns `402 Payment Required`, the device pays via Fiber and retries transparently.

## Architecture

```
wy_agentpay        ← top-level API (init, call, server)
    ├── wy_x402    ← HTTP 402 intercept + retry
    └── wy_fiber_rpc ← fnn JSON-RPC client (send_payment, new_invoice, settle...)
```

## Components

| Component | Purpose |
|-----------|---------|
| `wy_fiber_rpc` | Fiber node RPC — send payments, create/settle invoices, poll status |
| `wy_x402` | HTTP 402 interceptor — parse invoice, pay, retry with proof header |
| `wy_agentpay` | Unified payer + provider API |

## Quick Start

### Payer (device pays for API access)

```c
#include "wy_agentpay.h"

wy_agentpay_config_t cfg = {
    .fiber_rpc_url      = "http://192.168.1.1:8227",  // your fnn node
    .payment_timeout_ms = 30000,
};
wy_agentpay_init(&cfg);

char resp[2048];
int  status;
wy_agentpay_call("https://api.example.com/sensor-data",
                  "GET", NULL, resp, sizeof(resp), &status);
// 402 handled automatically — resp contains final response
```

### Provider (device charges for its data)

```c
wy_agentpay_server_t srv;
wy_agentpay_server_init(&srv, &cfg, 100);  // 100 shannons per request

// In your httpd handler — demand payment:
char challenge[512];
char payment_hash[66];
wy_agentpay_server_make_challenge(&srv, challenge, sizeof(challenge), payment_hash);
// Send HTTP 402 with challenge as body

// On retry — verify proof:
const char *proof = httpd_req_get_hdr_value_ptr(req, "X-Payment-Proof");
esp_err_t err = wy_agentpay_server_verify_proof(&srv, proof, payment_hash);
if (err == ESP_OK) { /* send resource */ }
```

## Configuration

All secrets are passed at runtime — **nothing is hardcoded**. Provision via:
- **NVS** (recommended for production) — store `fiber_rpc_url` in NVS, read at init
- **Kconfig** (development) — set via `menuconfig` (see example)
- **Hard init** (testing only) — set in `wy_agentpay_config_t` at build time

## x402 Payment Flow

```
Device                          Service
  │──── GET /resource ──────────▶│
  │◀─── 402 + fiber_invoice ─────│
  │                               │
  │──── fnn RPC: send_payment     │
  │     (blocks until settled)    │
  │                               │
  │──── GET /resource ──────────▶│
  │     X-Payment-Proof: pre:hash │
  │◀─── 200 + response ──────────│
```

## Tested Against

- **fnn v0.7.0** (Fiber Network node)
- **ESP-IDF v5.x**
- **ESP32-P4** (primary target), ESP32-S3, ESP32

## Dependencies

- ESP-IDF `esp_http_client`
- `cjson` (bundled or ESP-IDF managed component)
- Running **fnn** node accessible on LAN

## Known Limitations / TODOs

- `wy_agentpay_server_make_challenge`: payment hash extraction from invoice not yet implemented (requires BOLT11 decode). Server-side verify works via fnn status query instead.
- No TLS client cert support yet (for mutual-auth Fiber RPC)
- Payment replay protection: currently relies on fnn single-settlement; add nonce per request for production

## Credits

- Fiber Network RPC: [nervosnetwork/fiber](https://github.com/nervosnetwork/fiber)
- x402 protocol concept: [coinbase/x402](https://github.com/coinbase/x402)
- CKB address decode: `ckb_bech32.h` from [wyltek-embedded-builder](https://github.com/toastmanAu/wyltek-embedded-builder)
