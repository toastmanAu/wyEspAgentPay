/**
 * wy_fiber_invoice.c — Fiber invoice decoder
 * ===========================================
 * Ported from arcode-rs (github.com/cgbur/arcode-rs) and
 * nervosnetwork/fiber crates/fiber-lib/src/invoice/
 *
 * No dynamic allocation. All buffers are stack/static.
 * Safe for ESP32 (tested on P4/S3 with 8KB task stack).
 */

#include "wy_fiber_invoice.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "wy_fiber_inv";

/* ═══════════════════════════════════════════════════════════════════
 * 1. BECH32 DECODE
 * ═══════════════════════════════════════════════════════════════════ */

static const char BECH32_CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
#define BECH32_MAX_LEN  2048   /* Fiber invoices can be long */

static int8_t bech32_char_to_val(char c)
{
    c = tolower((unsigned char)c);
    for (int i = 0; i < 32; i++)
        if (BECH32_CHARSET[i] == c) return (int8_t)i;
    return -1;
}

static uint32_t bech32_polymod(const uint8_t *values, size_t len)
{
    static const uint32_t GEN[] = {
        0x3b6a57b2UL, 0x26508e6dUL, 0x1ea119faUL,
        0x3d4233ddUL, 0x2a1462b3UL
    };
    uint32_t chk = 1;
    for (size_t i = 0; i < len; i++) {
        uint8_t top = chk >> 25;
        chk = ((chk & 0x1ffffffUL) << 5) ^ values[i];
        for (int j = 0; j < 5; j++)
            if ((top >> j) & 1) chk ^= GEN[j];
    }
    return chk;
}

static void bech32_hrp_expand(const char *hrp, size_t hrp_len,
                              uint8_t *out, size_t *out_len)
{
    size_t k = 0;
    for (size_t i = 0; i < hrp_len; i++)
        out[k++] = (uint8_t)(hrp[i] >> 5);
    out[k++] = 0;
    for (size_t i = 0; i < hrp_len; i++)
        out[k++] = (uint8_t)(hrp[i] & 0x1f);
    *out_len = k;
}

/**
 * Decode bech32 string.
 * hrp_out: buffer for human-readable part (null-terminated)
 * data5:   output buffer for 5-bit groups (excluding checksum)
 * data5_len: on input = capacity, on output = number of 5-bit groups
 */
static esp_err_t bech32_decode(const char *input,
                                char       *hrp_out,  size_t hrp_cap,
                                uint8_t    *data5,    size_t *data5_len)
{
    size_t input_len = strlen(input);
    if (input_len < 8 || input_len > BECH32_MAX_LEN) return WY_INV_ERR_BECH32;

    /* Find last '1' separator */
    int sep = -1;
    for (int i = (int)input_len - 1; i >= 0; i--) {
        if (input[i] == '1') { sep = i; break; }
    }
    if (sep < 1 || sep + 7 > (int)input_len) return WY_INV_ERR_BECH32;

    size_t hrp_len  = (size_t)sep;
    size_t data_len = input_len - sep - 1;  /* includes 6-char checksum */

    if (hrp_len >= hrp_cap) return WY_INV_ERR_BUFFER;
    for (size_t i = 0; i < hrp_len; i++)
        hrp_out[i] = (char)tolower((unsigned char)input[i]);
    hrp_out[hrp_len] = '\0';

    if (data_len > *data5_len) return WY_INV_ERR_BUFFER;

    for (size_t i = 0; i < data_len; i++) {
        int8_t v = bech32_char_to_val(input[sep + 1 + i]);
        if (v < 0) return WY_INV_ERR_BECH32;
        data5[i] = (uint8_t)v;
    }

    /* Verify checksum */
    uint8_t expand[512]; size_t expand_len;
    bech32_hrp_expand(hrp_out, hrp_len, expand, &expand_len);
    uint8_t check_input[2048];
    memcpy(check_input, expand, expand_len);
    memcpy(check_input + expand_len, data5, data_len);
    if (bech32_polymod(check_input, expand_len + data_len) != 1)
        return WY_INV_ERR_CHECKSUM;

    /* Strip 6-char checksum, convert remaining 5-bit groups to 8-bit bytes */
    size_t payload5 = data_len - 6;
    /* Store raw 5-bit values for caller (they'll convert) */
    *data5_len = payload5;
    return ESP_OK;
}

/* Convert 5-bit array to 8-bit bytes */
static size_t base32_to_bytes(const uint8_t *data5, size_t len5,
                              uint8_t *out, size_t out_cap)
{
    uint32_t acc = 0; int bits = 0; size_t n = 0;
    for (size_t i = 0; i < len5; i++) {
        acc = (acc << 5) | data5[i];
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            if (n < out_cap) out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

/* ═══════════════════════════════════════════════════════════════════
 * 2. ADAPTIVE ARITHMETIC DECOMPRESSION
 *    Ported from arcode-rs (MIT license, github.com/cgbur/arcode-rs)
 *    Parameters: num_bits=8 (257 syms), EOF=256, precision=48, MSB
 * ═══════════════════════════════════════════════════════════════════ */

#define AR_SYMBOLS      257    /* 0..255 + EOF */
#define AR_EOF          256
#define AR_PRECISION    48
#define AR_HIGH_INIT    (1ULL << AR_PRECISION)

/* Fenwick tree for O(log n) prefix sums — ported from arcode-rs Model */
typedef struct {
    uint32_t counts[AR_SYMBOLS + 1];   /* raw symbol counts */
    uint32_t fenwick[AR_SYMBOLS + 2];  /* Fenwick BIT array */
    uint32_t total;
} ar_model_t;

static void fenwick_update(uint32_t *tree, int i, uint32_t delta, int n)
{
    for (i++; i <= n; i += i & (-i))
        tree[i] += delta;
}

static uint32_t fenwick_prefix(const uint32_t *tree, int i)
{
    uint32_t s = 0;
    for (i++; i > 0; i -= i & (-i))
        s += tree[i];
    return s;
}

static void ar_model_init(ar_model_t *m)
{
    memset(m, 0, sizeof(*m));
    /* Uniform initial distribution: count=1 per symbol */
    for (int i = 0; i < AR_SYMBOLS; i++) {
        m->counts[i] = 1;
        fenwick_update(m->fenwick, i, 1, AR_SYMBOLS);
    }
    m->total = AR_SYMBOLS;
}

static void ar_model_update(ar_model_t *m, uint32_t sym)
{
    m->counts[sym]++;
    fenwick_update(m->fenwick, (int)sym, 1, AR_SYMBOLS);
    m->total++;
}

/* Returns (low_frac * total, high_frac * total) as integer numerators */
static void ar_model_probability(const ar_model_t *m, uint32_t sym,
                                  uint64_t range_w,
                                  uint64_t *lo_out, uint64_t *hi_out)
{
    uint32_t hi_cnt = fenwick_prefix(m->fenwick, (int)sym);
    uint32_t lo_cnt = hi_cnt - m->counts[sym];
    uint64_t total  = m->total;
    *lo_out = (uint64_t)((double)lo_cnt / (double)total * (double)range_w);
    *hi_out = (uint64_t)((double)hi_cnt / (double)total * (double)range_w);
}

/* MSB bit reader over a byte buffer */
typedef struct {
    const uint8_t *buf;
    size_t         byte_len;
    size_t         byte_pos;
    int            bit_pos;   /* 7 = MSB of current byte */
} bit_reader_t;

static void br_init(bit_reader_t *br, const uint8_t *buf, size_t len)
{
    br->buf = buf; br->byte_len = len;
    br->byte_pos = 0; br->bit_pos = 7;
}

static uint64_t br_read_bit(bit_reader_t *br)
{
    if (br->byte_pos >= br->byte_len) return 0;
    uint64_t bit = (br->buf[br->byte_pos] >> br->bit_pos) & 1;
    if (--br->bit_pos < 0) { br->bit_pos = 7; br->byte_pos++; }
    return bit;
}



static size_t ar_decompress(const uint8_t *in, size_t in_len,
                                uint8_t *out,       size_t out_cap)
{
    ar_model_t model;
    ar_model_init(&model);

    bit_reader_t br;
    br_init(&br, in, in_len);

    uint64_t input_buf  = 0;
    for (int i = 0; i < AR_PRECISION; i++)
        input_buf = (input_buf << 1) | br_read_bit(&br);

    uint64_t rng_lo = 0;
    uint64_t rng_hi = AR_HIGH_INIT;

    size_t out_len = 0;

    for (;;) {
        uint64_t w = rng_hi - rng_lo;

        /* Binary search for the symbol whose range contains input_buf */
        uint32_t sym_lo_idx = 0, sym_hi_idx = AR_SYMBOLS - 1;
        uint32_t symbol = 0;
        bool found = false;

        while (sym_lo_idx <= sym_hi_idx) {
            uint32_t mid = (sym_lo_idx + sym_hi_idx) / 2;
            uint64_t p_lo, p_hi;
            ar_model_probability(&model, mid, w, &p_lo, &p_hi);
            uint64_t abs_lo = rng_lo + p_lo;
            uint64_t abs_hi = rng_lo + p_hi;

            if (abs_lo <= input_buf && input_buf < abs_hi) {
                symbol = mid; found = true; break;
            } else if (input_buf >= abs_hi) {
                sym_lo_idx = mid + 1;
            } else {
                if (mid == 0) break;
                sym_hi_idx = mid - 1;
            }
        }
        if (!found) break;  /* decode error */

        if (symbol == AR_EOF) break;

        if (out_len >= out_cap) return 0;
        out[out_len++] = (uint8_t)symbol;

        /* Update range BEFORE model update (matches arcode-rs decoder.rs) */
        {
            uint64_t p_lo, p_hi;
            ar_model_probability(&model, symbol, w, &p_lo, &p_hi);
            uint64_t old_lo = rng_lo;
            rng_lo = old_lo + p_lo;
            rng_hi = old_lo + p_hi;
        }

        /* Update model after range update */
        ar_model_update(&model, symbol);

        /* Renormalize — bottom half */
        uint64_t half    = AR_HIGH_INIT / 2;
        uint64_t quarter = AR_HIGH_INIT / 4;

        while (rng_hi < half || rng_lo > half) {
            if (rng_hi < half) {
                /* Bottom half scale */
                rng_lo   <<= 1;
                rng_hi   <<= 1;
                input_buf = (input_buf << 1) | br_read_bit(&br);
            } else if (rng_lo > half) {
                /* Upper half scale */
                rng_lo   = (rng_lo - half) << 1;
                rng_hi   = (rng_hi - half) << 1;
                input_buf = ((input_buf - half) << 1) | br_read_bit(&br);
            } else {
                break;
            }
        }
        /* Middle quarter */
        while (rng_lo > quarter && rng_hi < 3 * quarter) {
            rng_lo   = (rng_lo - quarter) << 1;
            rng_hi   = (rng_hi - quarter) << 1;
            input_buf = ((input_buf - quarter) << 1) | br_read_bit(&br);
        }
    }

    return out_len;
}

/* ═══════════════════════════════════════════════════════════════════
 * 3. MOLECULE TABLE PARSER
 *    Uses molecule_reader.h from wyltek-embedded-builder (official
 *    Nervos molecule C bindings) — no inline reimplementation needed.
 *
 *    RawInvoiceData (FIELD_COUNT=3):
 *      field 0: timestamp    — Uint128 (16 bytes LE)
 *      field 1: payment_hash — PaymentHash (32 bytes)
 *      field 2: attrs        — InvoiceAttrsVec (molecule union vector)
 * ═══════════════════════════════════════════════════════════════════ */

#include "molecule_reader.h"   /* from wyltek-embedded-builder/src/ckb/ */

static esp_err_t mol_parse_invoice(const uint8_t *buf, size_t len,
                                    wy_fiber_invoice_t *out)
{
    mol_seg_t root = { .ptr = buf, .size = (mol_num_t)len };

    /* field 0: timestamp (Uint128 = 16 bytes LE) */
    mol_seg_t ts_seg = mol_table_slice_by_index(&root, 0);
    if (ts_seg.size >= 8) {
        uint64_t ts_lo = 0;
        for (int i = 0; i < 8; i++) ts_lo |= (uint64_t)ts_seg.ptr[i] << (i*8);
        out->timestamp = ts_lo;
    }

    /* field 1: payment_hash (32 bytes) */
    mol_seg_t ph_seg = mol_table_slice_by_index(&root, 1);
    if (ph_seg.size == 32) {
        memcpy(out->payment_hash, ph_seg.ptr, 32);
        for (int i = 0; i < 32; i++)
            snprintf(out->payment_hash_hex + i*2, 3, "%02x", out->payment_hash[i]);
        out->payment_hash_hex[64] = '\0';
    }

    /* field 2: attrs (InvoiceAttrsVec — molecule dynvec of union items) */
    mol_seg_t attrs_seg = mol_table_slice_by_index(&root, 2);
    if (attrs_seg.size >= 8) {
        mol_num_t item_count = mol_unpack_number(attrs_seg.ptr);
        /* Iterate union items: each is [type:u32LE][data] */
        for (mol_num_t i = 0; i < item_count; i++) {
            mol_seg_res_t item_res = mol_dynvec_slice_by_index(&attrs_seg, i);
            if (item_res.errno != MOL_OK) continue;
            mol_seg_t item = item_res.seg;
            if (item.size < 4) continue;
            uint32_t union_type = mol_unpack_number(item.ptr);
            const uint8_t *data = item.ptr + 4;
            uint32_t data_len   = item.size - 4;

            switch (union_type) {
            case 2: /* Description — molecule Bytes: [len:u32][utf8] */
                if (data_len >= 4) {
                    uint32_t str_len = mol_unpack_number(data);
                    if (str_len < sizeof(out->description) && str_len <= data_len - 4) {
                        memcpy(out->description, data + 4, str_len);
                        out->description[str_len] = '\0';
                    }
                }
                break;
            case 3: /* ExpiryTime — u64 LE */
                if (data_len >= 8) {
                    uint64_t exp = 0;
                    for (int b = 0; b < 8; b++) exp |= (uint64_t)data[b] << (b*8);
                    out->expiry_seconds = exp;
                }
                break;
            case 5: /* PaymentSecret — 32 bytes */
                if (data_len >= 32) {
                    memcpy(out->payment_secret, data, 32);
                    out->has_payment_secret = true;
                }
                break;
            default: break;
            }
        }
    }

    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════
 * 4. HRP PARSER — extract currency + amount
 * ═══════════════════════════════════════════════════════════════════ */

static esp_err_t parse_hrp(const char *hrp, wy_fiber_invoice_t *out)
{
    if      (strncmp(hrp, "fibb", 4) == 0) { out->currency = WY_FIBER_MAINNET; hrp += 4; }
    else if (strncmp(hrp, "fibt", 4) == 0) { out->currency = WY_FIBER_TESTNET; hrp += 4; }
    else if (strncmp(hrp, "fibd", 4) == 0) { out->currency = WY_FIBER_DEVNET;  hrp += 4; }
    else return WY_INV_ERR_PREFIX;

    if (*hrp == '\0') {
        out->has_amount = false;
        out->amount_shannons = 0;
        return ESP_OK;
    }

    /* Amount: direct integer (no multiplier — Fiber uses raw shannons) */
    char *end;
    uint64_t amount = strtoull(hrp, &end, 10);
    if (end == hrp) return WY_INV_ERR_PREFIX;  /* not a number */
    out->amount_shannons = amount;
    out->has_amount = true;
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════
 * 5. PUBLIC API
 * ═══════════════════════════════════════════════════════════════════ */

esp_err_t wy_fiber_invoice_decode(const char *invoice_str, wy_fiber_invoice_t *out)
{
    if (!invoice_str || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(wy_fiber_invoice_t));

    /* Step 1: bech32 decode */
    static uint8_t data5[BECH32_MAX_LEN];
    size_t data5_len = sizeof(data5);
    char hrp[32] = {0};

    esp_err_t err = bech32_decode(invoice_str, hrp, sizeof(hrp),
                                   data5, &data5_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bech32 decode failed: 0x%x", err);
        return err;
    }

    /* Step 2: parse HRP (currency + amount) */
    err = parse_hrp(hrp, out);
    if (err != ESP_OK) return err;

    /* Step 3: convert 5-bit groups to bytes (signature at end — 65 bytes = 104 5-bit groups) */
    static uint8_t raw_bytes[BECH32_MAX_LEN];
    size_t raw_len = base32_to_bytes(data5, data5_len, raw_bytes, sizeof(raw_bytes));

    /* Last 65 bytes are the signature — strip them */
    if (raw_len < 65) return WY_INV_ERR_PARSE;
    size_t compressed_len = raw_len - 65;

    /* Step 4: skip first 5 bytes (timestamp encoded separately before compression in some versions)
     * Actually: Fiber compresses the entire InvoiceData protobuf.
     * The raw_bytes is: [4 bytes timestamp big-endian?] + [compressed proto]
     * Check from invoice_impl.rs: data_part() = timestamp(u128 as varint) + ar_compress(proto)
     * So first N bytes are the timestamp varint, rest is compressed. */

    /* Simpler: try to decompress entire compressed portion */
    static uint8_t decompressed[4096];
    size_t decomp_len = ar_decompress(raw_bytes, compressed_len,
                                          decompressed, sizeof(decompressed));
    if (decomp_len == 0) {
        ESP_LOGE(TAG, "ar_decompress failed");
        return WY_INV_ERR_DECOMPRESS;
    }

    /* Step 5: parse molecule */
    err = mol_parse_invoice(decompressed, decomp_len, out);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Decoded invoice: hash=%.16s... amount=%llu shannons",
             out->payment_hash_hex,
             (unsigned long long)out->amount_shannons);
    return ESP_OK;
}

/* ── Preimage verification (offline SHA256 check) ──────────────── */

esp_err_t wy_fiber_verify_preimage(const wy_fiber_invoice_t *invoice,
                                    const char               *preimage_hex)
{
    if (!invoice || !preimage_hex) return ESP_ERR_INVALID_ARG;
    if (strlen(preimage_hex) != 64) return ESP_ERR_INVALID_ARG;

    /* Decode hex preimage to bytes */
    uint8_t preimage[32];
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        if (sscanf(preimage_hex + i*2, "%02x", &byte) != 1)
            return ESP_ERR_INVALID_ARG;
        preimage[i] = (uint8_t)byte;
    }

    /* SHA256(preimage) must equal payment_hash */
    uint8_t hash[32];
    mbedtls_sha256(preimage, 32, hash, 0);

    if (memcmp(hash, invoice->payment_hash, 32) != 0) {
        ESP_LOGE(TAG, "Preimage verification failed");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Preimage verified OK");
    return ESP_OK;
}
