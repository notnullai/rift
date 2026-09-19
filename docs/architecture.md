# RIFT Architecture

## Overview
RIFT is a deterministic, CUDA-free, fixed-point (Q16.16) spiking neural network engine for passive network anomaly detection. It produces class scores only — no control path, no packet modification.

## Design Principles
- **Zero blast radius**: No `drop`, `redirect`, `modify` ops. Output = class scores only.
- **Bit-exactness**: Same output across x86_64, aarch64, Cortex-A, eBPF, SmartNIC ARM.
- **No dynamic allocation after init**: All mutable state in caller-supplied arena.
- **eBPF compatible**: Bounded loops, no globals, 512-byte stack, brain in `.rodata`.

## Components

### Brain Format
- **Header** (96 bytes): magic `"RIFTBRA1"`, version, geometry, leak rate, score window, predictor params count, encoder geometry hash.
- **Body**: Affurrent/recurrent weights (ternary: -1/0/+1), thresholds, predictor EWMA alphas.
- Typical size: ~150 KB (input_bits=640, hidden=512, class=8, score_window=512).

### Encoder
Bipolar surprise encoder. Each feature produces a thermometer spike train:
- `surprise = feature - predictor_state` (clamped to [-max_val, +max_val])
- `bits_set = |surprise| * thermometer_bits / max_val`
- Polarity: positive surprise → positive bits; negative → negative bits; zero → no bits

### Predictor
First-order EWMA (Exponentially Weighted Moving Average) per feature:
- `prediction += alpha * (feature - prediction)`
- Same code links into runtime and trainer (no config drift).
- Alpha embedded in brain file.

### Tick Loop
```
1. Pack input spikes into fixed-point representation
2. Apply leak: V *= (1 - leak_rate) + input
3. Afferent: compute excitatory/inhibitory inputs from input spikes
4. Recurrent: compute recurrent connections between hidden neurons
5. Apply thresholds: neuron spikes if V >= threshold
6. Update score accumulators: circular window, popcount → Q16.16 score
```

### Safety Layer
- Pre-tick: check budget and watchdog
- Post-tick: update heartbeat, check fault state
- Heartbeat stalls when tick loop hung → host firewall uses own rules

### HAL (Hardware Abstraction Layer)
Platform-specific functions:
- `read_telemetry_frame()` - get next network frame
- `signal_anomaly(class_id, score)` - report anomaly
- `now_us()` - timestamp
- `kick_hardware_watchdog()` - prevent watchdog reset

## Data Flow
```
Network Frame
    ↓
Feature Extraction (log-scaled)
    ↓
Predictor (EWMA) → Surprise
    ↓
Encoder (bipolar thermometer)
    ↓
SNN Core (leak, afferent, recurrent, threshold)
    ↓
Score Accumulator (circular window, Q16.16)
    ↓
Class Scores (0-65536 per class)
```

## Fixed-Point Format
- **Q16.16**: 16 integer bits, 16 fractional bits
- Range: -32768.0 to +32767.99998
- Resolution: 1/65536 ≈ 0.0000153
- All arithmetic in tick path uses integer operations only
