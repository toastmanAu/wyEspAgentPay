# wyEspAgentPay Roadmap

## v0.1.0 — Current (scaffold)
- [x] `wy_fiber_rpc` — fnn v0.7.x RPC client (send_payment, poll status, new/settle/cancel hold invoices)
- [x] `wy_x402` — HTTP 402 interceptor with correct `preimage:hash` proof header and proper client reinit on retry
- [x] `wy_agentpay` — unified payer + provider API, zero hardcoded secrets
- [x] Example `main.c` with Kconfig-driven config (WiFi SSID, fnn URL, target URL)
- [x] Tested RPC endpoints against live fnn v0.7.0

---

## v0.2.0 — Planned

### Fiber Invoice Decoder (`wy_fiber_invoice_decode`)

**Use case:** Server-side payment hash extraction without an fnn RPC roundtrip.

**Current situation:** When the ESP32 acts as a *provider* (charges for data it serves), it creates a hold invoice via `wy_fiber_new_hold_invoice()` and returns a `402` challenge. On the client's retry, it needs to verify the `X-Payment-Proof` header matches the invoice it issued. Currently this requires a second RPC call to fnn (`get_payment` → check status). That works but adds latency and a network dependency.

**What `wy_fiber_invoice_decode()` would enable:**
- Extract `payment_hash` from a Fiber invoice string locally, on-device, without calling fnn
- Verify `preimage:hash` proof offline (SHA256(preimage) == payment_hash)
- Fully self-contained provider flow — no fnn needed for verification after payment settles

**Why it's non-trivial:** Fiber invoices are BOLT11-derived (bech32, tagged fields, secp256k1 signature) but add **adaptive arithmetic compression** (`ar_decompress`) on the data part before bech32 encoding. A standard BOLT11 decoder won't work — you need to decompress first, then parse.

**Implementation plan:**
1. Port `ar_decompress` from Fiber's Rust (`crates/fiber-lib/src/invoice/utils.rs`) to ~150 lines of bare-metal C (no malloc, fixed ring buffer)
2. Add bech32 decode (reuse/adapt `ckb_bech32.h` from wyltek-embedded-builder)
3. Parse tagged fields: extract `payment_hash` (field type `p`, 256-bit), `amount`, `description`
4. Validate secp256k1 signature (optional — skip for embedded, trust fnn settlement instead)

**Research:** See `research/findings/fiber-bolt11-decode.md` — Fiber invoice format fully documented including Rust source analysis. Currency prefixes: `fibb` (mainnet), `fibt` (testnet), `fibd` (devnet).

**Effort estimate:** ~2–3 days. Blocked on: arithmetic codec port.

---

### Other v0.2.0 items
- [ ] NVS provisioning helper — store `fiber_rpc_url` in NVS at first boot, read at init
- [ ] Configurable retry count + backoff for 402 → payment → retry loop
- [ ] Payment replay protection — per-request nonce in invoice description field

---

## v0.3.0 — Future

- [ ] TLS mutual auth for fnn RPC (for production deployments where fnn is not on LAN)
- [ ] Multi-hop payment support (specify routing hints in invoice)
- [ ] Port to ESP-IDF managed components registry
- [ ] Arduino wrapper (`wyEspAgentPay.h`) for non-IDF users
