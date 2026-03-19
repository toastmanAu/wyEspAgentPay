# wyEspAgentPay

ESP-IDF component library for x402 HTTP micropayments over Nervos Fiber Network.

An ESP32 device makes a normal HTTP request. If the server returns `402 Payment Required` with a Fiber invoice, the library automatically pays it and retries — no application code changes needed.

## Architecture

```
wyEspAgentPay/
├── components/
│   ├── wy_fiber_rpc/    — fnn JSON-RPC client (send_payment, get_payment, new_invoice)
│   ├── wy_x402/         — 402 intercept + proof header builder
│   └── wy_agentpay/     — top-level API (init, get, post, make_402, verify_proof)
└── example/             — WiFi + demo app (payer + provider roles)
```

## Quick Start

```c
wy_agentpay_config_t cfg = {
    .fiber_rpc_url      = CONFIG_WY_FIBER_RPC_URL,   // from menuconfig
    .payment_timeout_ms = 30000,
};
wy_agentpay_init(&cfg);

// That's it — any request auto-handles 402
char response[1024];
wy_agentpay_get("http://service.local/api/data", response, sizeof(response));
```

## Configuration

All config via `idf.py menuconfig → wyEspAgentPay`:

| Option | Default | Description |
|--------|---------|-------------|
| `WY_FIBER_RPC_URL` | `http://192.168.1.10:8227` | fnn RPC endpoint |
| `WY_PAYMENT_TIMEOUT_MS` | `30000` | Max payment wait (ms) |
| `WY_HTTP_TIMEOUT_MS` | `10000` | HTTP request timeout (ms) |

For production: provision `fiber_rpc_url` via NVS at first boot — don't hardcode it.

## x402 Protocol

The library implements the [x402 payment protocol](https://www.x402.org) adapted for Fiber:

**402 body schema (server sends):**
```json
{
  "x402Version": 1,
  "payment": {
    "scheme": "fiber",
    "fiber_invoice": "fibt1...",
    "payment_hash": "0x...",
    "amount_shannons": 1000,
    "description": "API access"
  }
}
```

**Retry header (client sends after payment):**
```
X-Payment-Proof: <preimage>:<payment_hash>
```

## Roles

**Payer** — device consuming a paid service:
```c
wy_agentpay_get(url, response, sizeof(response));  // handles 402 automatically
```

**Provider** — device serving a paid resource:
```c
// Generate 402 response body with live Fiber invoice
char body[600];
wy_agentpay_make_402(1000, "Sensor data", body, sizeof(body));
// Send body with HTTP 402 status

// Later: verify the client paid
wy_agentpay_verify_proof(request_header, expected_hash);
```

## Dependencies

- ESP-IDF ≥ 5.0
- `esp_http_client`
- `cJSON` (bundled in ESP-IDF or bring your own)
- `mbedtls` (for proof verification — optional, partial impl included)

## Board Targets

Tested target: **Guition JC4880P433** (ESP32-P4, 32MB PSRAM)  
Compatible with any ESP32 variant with WiFi and sufficient heap (~32KB free).

## Related Projects

- [fnn (Fiber Network Node)](https://github.com/nervosnetwork/fiber) — the payment channel node this talks to
- [ckb-access](https://github.com/toastmanAu/ckb-access) — one-command fnn installer
- [wyltek-embedded-builder](https://github.com/toastmanAu/wyltek-embedded-builder) — board abstraction layer

## Known TODOs

- [ ] Full `preimage:hash` proof verification via `mbedtls_sha256`
- [ ] Two-step `send_payment` + cache hash for correct proof format
- [ ] TLS support for remote fnn endpoints
- [ ] NVS provisioning helper for `fiber_rpc_url`
- [ ] Test on hardware (JC4880P433)
