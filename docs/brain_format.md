# RIFT Brain Format Specification

**Version**: 2.0
**Endianness**: Little-endian only. Big-endian hosts are unsupported.
**Magic**: `RIFTBRA2` (8 bytes)

---

## Overview

The RIFT brain format stores a frozen ternary SNN classifier for passive network anomaly detection. It is self-contained, validated at load, and includes predictor parameters and encoder geometry to prevent config-drift deployment bugs.

The brain is strictly numeric. Class names, per-class thresholds, and other deployment metadata live in a separate `.riftmeta` JSON sidecar file.

---

## File Layout

```
Header (96 bytes, packed)
Encoder Geometry Block (648 bytes, packed, fixed)
Body (variable)
```

Total typical size: ~150 KB (input_bits=640, hidden=512, class=8, feature_count=16, score_window=512)

---

## Header (96 bytes, little-endian, packed)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 8 | `magic` | ASCII `"RIFTBRA2"` |
| 8 | 4 | `version` | Format version (2) |
| 12 | 4 | `flags` | Reserved (must be 0; reject non-zero) |
| 16 | 4 | `input_bits` | Number of input feature bits (multiple of 32) |
| 20 | 4 | `hidden_neurons` | Total hidden + class neurons (multiple of 32) |
| 24 | 4 | `class_neurons` | Number of class neurons (≤64) |
| 28 | 4 | `feature_count` | Number of continuous features (≤32) |
| 32 | 4 | `input_words` | `input_bits / 32` |
| 36 | 4 | `hidden_words` | `hidden_neurons / 32` |
| 40 | 4 | `class_words` | `(score_window + 31) / 32` |
| 44 | 4 | `fire_threshold` | Spike threshold (int32) |
| 48 | 4 | `leak_q16` | Leak rate in Q16.16 (uint32) |
| 52 | 4 | `score_window` | Rolling window size in ticks |
| 56 | 4 | `predictor_params_count` | = 2 × feature_count |
| 60 | 8 | `encoder_geometry_hash` | FNV-1a 64-bit hash of encoder geometry block |
| 68 | 28 | `reserved` | 7 × uint32_t for future use |

**Struct is packed** (no padding). Use `#pragma pack(1)` or equivalent.

---

## Encoder Geometry Block (648 bytes, little-endian, packed)

Fixed-size block immediately following header. Contains full encoder configuration so runtime can verify bit-for-bit match.

```c
typedef struct {
    int32_t surprise_min_q16;      // Min surprise value (log space, Q16.16)
    int32_t surprise_max_q16;      // Max surprise value (log space, Q16.16)
    int32_t surprise_dead_zone_q16; // Dead zone around 0 (Q16.16)
    int32_t thermometer_bits;       // Bits per polarity (positive/negative)
    int32_t absolute_bits;          // Additional absolute channel bits (0 if none)
} rift_feature_geometry;           // 20 bytes per feature

typedef struct {
    uint32_t n_features;            // Number of continuous features (≤32)
    uint32_t n_absolute_channels;   // Number of absolute-channel features
    rift_feature_geometry features[32];
} rift_encoder_geometry_block;     // 648 bytes total
```

**Bit layout per feature:**
- `thermometer_bits` for positive surprise (thermometer)
- `thermometer_bits` for negative surprise (thermometer)  
- `absolute_bits` for one-hot/absolute channel (0 for continuous features)

Total input bits: `input_bits = Σ(2 × thermometer_bits + absolute_bits) + one_hot_bits`

**Example (typical):** 16 features × 20 bits/polarity × 2 polarities = 640 bits. No absolute bits.

---

## Body (concatenated, no padding)

### 1. Afferent Excitatory Masks
- **Size**: `hidden_neurons × input_words × 4 bytes`
- **Layout**: `aff_exc[n * input_words + w]`
- **Bit meaning**: Bit `j & 31` of word `j >> 5` = input line `j`'s weight for post neuron `n`
- **Values**: 0 or 1 (ternary: +1)

### 2. Afferent Inhibitory Masks
- **Size**: `hidden_neurons × input_words × 4 bytes`
- **Layout**: `aff_inh[n * input_words + w]`
- **Bit meaning**: Bit `j & 31` of word `j >> 5` = input line `j`'s weight for post neuron `n`
- **Values**: 0 or 1 (ternary: -1)

### 3. Recurrent Excitatory Masks
- **Size**: `hidden_neurons × hidden_words × 4 bytes`
- **Layout**: `rec_exc[n * hidden_words + w]`
- **Bit meaning**: Bit `p & 31` of word `p >> 5` = pre neuron `p`'s weight for post neuron `n`
- **Values**: 0 or 1 (ternary: +1)

### 4. Recurrent Inhibitory Masks
- **Size**: `hidden_neurons × hidden_words × 4 bytes`
- **Layout**: `rec_inh[n * hidden_words + w]`
- **Bit meaning**: Bit `p & 31` of word `p >> 5` = pre neuron `p`'s weight for post neuron `n`
- **Values**: 0 or 1 (ternary: -1)

### 5. Neuron Thresholds
- **Size**: `hidden_neurons × 4 bytes`
- **Layout**: `threshold[n]`
- **Class neurons** (indices `[0, class_neurons)`): use `fire_threshold` from header
- **Hidden neurons** (indices `[class_neurons, hidden_neurons)`): calibrated per-neuron thresholds

### 6. Predictor Parameters
- **Size**: `predictor_params_count × 4 bytes` (where `predictor_params_count = 2 × feature_count`)
- **Layout**: `predictor_params_q16[i]`
- **Reference predictor** (first-order EWMA per feature): `[alpha_q16, init_q16]` per feature
- Stored as flat array: `[alpha_0, init_0, alpha_1, init_1, ...]`

---

## Mask Layout Details

The ternary weights are stored as two bitmasks per connection (exc/inh):

```
Weight = +1  → bit set in exc mask, clear in inh mask
Weight =  0  → bit clear in both masks
Weight = -1  → bit clear in exc mask, set in inh mask
```

**Afferent** (input → hidden):
- Post-neuron major: `aff_exc[n][w]`, `aff_inh[n][w]`
- Input line `j` → bit `j & 31` of word `j >> 5`

**Recurrent** (hidden → hidden):
- Post-neuron major: `rec_exc[n][w]`, `rec_inh[n][w]`
- Pre-neuron `p` → bit `p & 31` of word `p >> 5`

This is the KHALM engine layout.

---

## Encoder Geometry Hash

The `encoder_geometry_hash` is a 64-bit FNV-1a hash of the **entire encoder geometry block** (648 bytes):

```c
uint64_t rift_compute_encoder_geometry_hash(const rift_encoder_geometry_block* geom);
```

**Purpose**: The encoder's geometry (which features, in what order, at what thermometer resolution, with what surprise bounds, dead zones) determines which input line corresponds to which feature. The brain's weights are wired to line positions, not feature names. A geometry mismatch between training and deployment makes the brain's weights meaningless.

**Validation**: `rift_load` computes the hash from the brain's encoder geometry block and compares it to the header's stored hash. On mismatch, load fails with `RIFT_ERR_GEOM_HASH`.

---

## Predictor Parameter Block

The reference predictor is a first-order Exponentially Weighted Moving Average (EWMA) **per continuous feature**:

```
state = state + alpha * (observed - state)
```

Parameters per feature (2 × feature_count total):
- `alpha_q16`: Smoothing factor in Q16.16 (e.g., 6554 ≈ 0.1)
- `init_q16`: Initial state in Q16.16 (typically 0)

**Note**: Not per input bit. A thermometer's bits are perfectly correlated (they encode one continuous value). One EWMA per feature.

---

## Validation Rules (enforced by `rift_load`)

1. **Magic**: Must be exactly `"RIFTBRA2"`
2. **Version**: Must be 2
3. **Flags**: Must be 0 (reject non-zero)
4. **Geometry**:
   - `input_bits % 32 == 0`
   - `hidden_neurons % 32 == 0`
   - `class_neurons ≤ 64`
   - `feature_count ∈ [1, 32]`
   - `score_window ∈ [1, 4096]`
   - `predictor_params_count == 2 × feature_count`
   - `input_words == (input_bits + 31) / 32`
   - `hidden_words == (hidden_neurons + 31) / 32`
   - `class_words == (score_window + 31) / 32`
5. **Size**: `file_size == 96 + 648 + body_size`
6. **Encoder geometry hash**: Must match computed hash of encoder geometry block
7. **Arena alignment**: Caller-supplied arena must be 4-byte aligned
8. **Arena size**: Must be ≥ `rift_get_required_memory()` result

---

## Error Codes

| Code | Value | Meaning |
|------|-------|---------|
| `RIFT_OK` | 0 | Success |
| `RIFT_ERR_ARG` | -1 | Null pointer, index out of range |
| `RIFT_ERR_FORMAT` | -2 | Brain file format violation |
| `RIFT_ERR_GEOMETRY` | -3 | Shape violation |
| `RIFT_ERR_ARENA` | -4 | Arena too small or unaligned |
| `RIFT_ERR_GEOM_HASH` | -5 | Encoder geometry hash mismatch |
| `RIFT_ERR_SAFETY` | -6 | Safety layer in fault state |

---

## Tools

### `brain_info`
Prints brain geometry, encoder geometry block, hash, predictor params, and size breakdown:
```
brain_info tests/golden/test_brain.rift
```

### `brain_gen`
Generates a test brain with random sparse ternary weights and encoder geometry:
```
brain_gen
```

### `brain_validate`
Validates brain file format, geometry, and hash:
```
brain_validate tests/golden/test_brain.rift
```

---

## Class Names & Thresholds: `.riftmeta` Sidecar

The brain is strictly numeric. Class names and per-class thresholds are deployment metadata, stored in a JSON sidecar:

```
brain.rift        # binary, eBPF-loadable, no strings
brain.riftmeta    # JSON: class names, thresholds, deployment info
```

```json
{
  "brain_sha256": "a3f5...",
  "class_names": ["NORMAL_ABSENT", "SYN_FLOOD", "UDP_AMP", "HTTP_FLOOD"],
  "class_thresholds_q16": [0, 45875, 52428, 45875],
  "trained_on": "2026-09-15T14:30:00Z",
  "trainer_git_sha": "b6a9f3...",
  "notes": "Trained on Q3 production captures; FPR target 0.001"
}
```

- `brain_sha256` ties metadata to the specific brain. Regeneration invalidates sidecar.
- Missing sidecar → `rift_replay` falls back to `class_0 … class_n`. Not a load failure.

---

## Size Example (Typical v2)

| Section | Size |
|---------|------|
| Header (packed) | 96 B |
| Encoder geometry block | 648 B |
| `aff_exc` + `aff_inh` | 512 × 20 × 4 × 2 = 81,920 B |
| `rec_exc` + `rec_inh` | 512 × 16 × 4 × 2 = 65,536 B |
| `threshold` | 512 × 4 = 2,048 B |
| `predictor_params` | 32 × 4 = 128 B |
| **Total** | **~150 KB** |

Fits comfortably in eBPF `.rodata` map or SmartNIC on-chip memory.