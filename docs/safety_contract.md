# RIFT Safety Contract

## Overview

Rift is a **zero-blast-radius** anomaly detector. It has no control path — no `drop`, `redirect`, or `modify` operations. Its only output is class scores. The host firewall is always authoritative.

## Safety Guarantees

### 1. Heartbeat Contract

- `rift_safety_heartbeat()` must be called periodically by the tick loop.
- If the heartbeat stalls (elapsed time > `tick_budget_us` for `watchdog_misses` consecutive ticks), the safety layer enters **fault state**.
- `rift_safety_ok()` returns `false` in fault state.
- The host firewall must check `rift_safety_ok()` before relying on Rift scores.
- When heartbeat is stale, the host continues with its own rules. Rift's extra context is lost, but the host remains protected.

### 2. Tick Budget

- Each tick must complete within `tick_budget_us` microseconds.
- The budget is configured in `rift_safety_config.tick_budget_us`.
- `test_tick_budget` validates: 100k ticks, max tick time < budget.
- Typical budget: 1,000 us (1 ms) for 1 kHz network anomaly detection (or 100 us for 10 kHz fast-path).

### 3. Score Output

- Scores are Q16.16 fixed-point values in range `[0, 65536]` (0.0 to 1.0).
- One score per class neuron (up to 64 classes).
- No softmax — class neurons are independent. SYN flood + UDP amplification can co-occur.
- Scores are read-only from the host's perspective (shared memory one-way).

### 4. Memory Safety

- No dynamic allocation after `rift_load()`. All mutable state is in caller-supplied arena.
- `rift_get_required_memory()` returns exact arena size needed.
- Arena must be 8-byte aligned (suitable for 64-bit architectures and timestamps).
- Brain data pointer must remain valid for the lifetime of the engine (not copied to arena).

### 5. Cross-Platform Determinism

- Q16.16 fixed-point everywhere in tick path. No floating point anywhere in the tick arithmetic.
- Byte-identical output across x86_64, aarch64, Cortex-A, eBPF, SmartNIC ARM.
- `test_platform_invariance` validates SHA-256 of canonical replay matches across platforms.

## Integration Checklist

| # | Check | Test |
|---|-------|------|
| 1 | Heartbeat stalls when tick loop hung | `test_safety` |
| 2 | Host firewall operates with own rules when heartbeat stale | `test_safety` |
| 3 | Shared memory region not writable by host (one-way) | Architecture (HAL design) |
| 4 | Tick time distribution: max < budget over 100k ticks | `test_tick_budget` |
| 5 | Full loop + mock HAL: 10k ticks, safety ok, scores valid | `test_integration` |
| 6 | Engine reset produces zero scores, re-run produces non-zero | `test_integration` |
| 7 | Watchdog kicked every tick | `test_integration` |
| 8 | Bit-exact replay across platforms | `test_platform_invariance` |

## HAL Interface

The HAL (`rift_hal`) provides five callbacks:

| Callback | Purpose |
|----------|---------|
| `read_telemetry_frame` | Read network telemetry into encoder input |
| `signal_anomaly` | Notify host of anomaly scores |
| `now_us` | Current timestamp in microseconds |
| `kick_hardware_watchdog` | Reset hardware watchdog timer (platform-dependent) |
| `log` | Structured logging |

> **Note on `kick_hardware_watchdog`**: On bare-metal SmartNIC ARM cores/DPUs, this invokes the platform's hardware watchdog MMIO register. Inside Linux kernel eBPF, there is no hardware watchdog peripheral — implemented as a no-op (or BPF map update), as the BPF verifier already enforces instruction bounds and the host daemon monitors the heartbeat counter in the BPF map.

The HAL is the only boundary between Rift and the host. All Rift outputs flow through `signal_anomaly`. There is no path for Rift to modify packets, redirect traffic, or access host state.

## Failure Modes

| Mode | Detection | Host Action |
|------|-----------|-------------|
| Heartbeat stall | `rift_safety_ok() == false` | Use own firewall rules, ignore Rift scores |
| Tick overrun | Safety fault after N misses | Same as heartbeat stall |
| Brain format error | `rift_load()` returns error | Reject brain, refuse to start |
| Geometry hash mismatch | `rift_load()` returns `RIFT_ERR_GEOM_HASH` | Reject brain, refuse to start |
| Arena too small | `rift_load()` returns `RIFT_ERR_ARENA` | Allocate more memory, retry |
