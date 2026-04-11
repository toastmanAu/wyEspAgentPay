/**
 * wy_auth_agentpay.h — WyAuth × wyEspAgentPay integration bridge
 * ===============================================================
 * Bridges the two previously-disconnected halves of the embedded CKB stack:
 *
 *   WyAuth (identity)  +  wyEspAgentPay (payment)
 *         │                       │
 *         └───── WyAuthAgentPay ──┘
 *
 * Before this component, you had to manually thread WyAuthCredential.address
 * through to payment calls and build the hold invoice / poll / settle flow
 * yourself. This component does it in one call.
 *
 * Typical POS / access-gate flow:
 *
 *   1.  Customer scans JoyID QR on device screen
 *   2.  wa_ap_authenticate() → WyAuthCredential (address, pubkey, verified)
 *   3a. wa_ap_accept_fiber()  → generate hold invoice, wait for HTLC, settle
 *   3b. wa_ap_accept_l1()     → generate nervos: QR, poll indexer for cell
 *   4.  Grant access / dispense item
 *
 * FiberQuest tournament controller flow:
 *
 *   wa_ap_pay_to_credential()  → pay a winner identified by WyAuthCredential
 *
 * Thread safety: all calls are synchronous and blocking. Call from a
 * dedicated FreeRTOS task if you need non-blocking behaviour.
 */
#pragma once

#include "esp_err.h"
#include "wy_fiber_rpc.h"
#include "wy_agentpay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Result ───────────────────────────────────────────────────── */

typedef struct {
    char     payer_address[128];  /* CKB address of authenticated payer */
    char     payment_hash[66];    /* Fiber payment hash (Fiber path only) */
    uint64_t amount_shannons;     /* Amount actually received */
    bool     fiber;               /* true = Fiber L2, false = L1 on-chain */
    bool     identity_verified;   /* true = WyAuth signature passed */
} wa_ap_payment_t;

/* ── Config ───────────────────────────────────────────────────── */

typedef struct {
    /* Fiber node RPC URL — required for Fiber flows */
    char fiber_rpc_url[256];
    /* CKB node/indexer RPC URL — required for L1 flows */
    char ckb_rpc_url[256];
    /* Merchant CKB address (receives L1 payments) */
    char merchant_address[128];
    /* WyAuth callback base URL (where JoyID redirects after auth) */
    char auth_callback_url[256];
    /* App name shown in JoyID prompt */
    char app_name[64];
    /* Payment timeout in ms (0 = 60s default) */
    uint32_t payment_timeout_ms;
} wa_ap_config_t;

/**
 * @brief Initialise the bridge. Verifies Fiber node connectivity.
 *        Must be called once before any wa_ap_* calls.
 */
esp_err_t wa_ap_init(const wa_ap_config_t *cfg);

/**
 * @brief Generate a JoyID auth URL and wait for the credential callback.
 *        Blocks until the customer completes auth or timeout expires.
 *        The device should display a QR of the returned URL while waiting.
 *
 * @param session_id    Unique session ID (caller-generated, e.g. UUID or millis())
 * @param url_out       Buffer for the JoyID redirect URL to show as QR
 * @param url_len       Size of url_out
 * @param cred_json_out Buffer for the raw credential JSON from callback
 * @param cred_json_len Size of cred_json_out
 * @param timeout_ms    0 = use config default
 *
 * @return ESP_OK + cred_json_out populated on success
 *         ESP_ERR_TIMEOUT if customer didn't scan in time
 */
esp_err_t wa_ap_authenticate(const char *session_id,
                              char       *url_out,
                              size_t      url_len,
                              char       *cred_json_out,
                              size_t      cred_json_len,
                              uint32_t    timeout_ms);

/**
 * @brief Accept a Fiber (L2) payment from an authenticated payer.
 *
 * Flow:
 *   1. Generate hold invoice on local Fiber node (wyEspAgentPay)
 *   2. Encode invoice + payer address into challenge JSON
 *   3. Poll for HTLC arrival (payment_hash match)
 *   4. Settle the hold invoice, revealing preimage
 *   5. Return payment details
 *
 * The payer_address from cred is embedded in the invoice description so
 * you can verify which authenticated user paid — no separate identity check
 * needed after payment settles.
 *
 * @param cred_json     Raw credential JSON from wa_ap_authenticate()
 * @param amount_shannons  Amount to charge
 * @param result        Populated on success
 */
esp_err_t wa_ap_accept_fiber(const char   *cred_json,
                              uint64_t      amount_shannons,
                              wa_ap_payment_t *result);

/**
 * @brief Accept an L1 on-chain payment from an authenticated payer.
 *
 * Flow:
 *   1. Verify the credential (WyAuth P-256 verify)
 *   2. Build nervos: payment URI for merchant_address
 *   3. Write the URI to qr_out (caller displays as QR)
 *   4. Poll CKB indexer for incoming cell >= amount_shannons
 *   5. Return payment details
 *
 * @param cred_json        Raw credential JSON from wa_ap_authenticate()
 * @param amount_shannons  Amount to charge
 * @param qr_out           Buffer for nervos: URI to display as QR
 * @param qr_len           Size of qr_out
 * @param result           Populated on success
 */
esp_err_t wa_ap_accept_l1(const char      *cred_json,
                            uint64_t         amount_shannons,
                            char            *qr_out,
                            size_t           qr_len,
                            wa_ap_payment_t *result);

/**
 * @brief Pay an authenticated counterparty via Fiber.
 *
 * Used when the ESP32 is the PAYER — e.g. paying a tournament winner,
 * paying a service provider identified by their JoyID credential.
 *
 * The counterparty must have a Fiber invoice ready. This call:
 *   1. Verifies the counterparty credential
 *   2. Sends payment to the invoice
 *   3. Confirms settlement
 *
 * @param payee_cred_json  Credential JSON of the party to pay
 * @param invoice          Fiber invoice from the payee (fibn1...)
 * @param result           Payment result
 */
esp_err_t wa_ap_pay_to_credential(const char       *payee_cred_json,
                                   const char       *invoice,
                                   wy_payment_result_t *result);

#ifdef __cplusplus
}
#endif
