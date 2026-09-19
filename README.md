<div align="center">

# RIFT

**A passive Q16.16 fixed-point SNN anomaly detector for network traffic.**

[![CI](https://github.com/notnullai/rift/actions/workflows/ci.yml/badge.svg)](https://github.com/notnullai/rift/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/notnullai/rift)](https://github.com/notnullai/rift/releases)

</div>

> Rift gives security teams byte-reproducible anomaly alerts without touching the packet path.

---

## What Rift is

Rift is a **passive anomaly detector**. It observes network telemetry, classifies it against a trained baseline, and emits per-class confidence scores. It returns `XDP_PASS` on every packet. It has **no control path to the wire**.

Rift is a **Q16.16 fixed-point spiking neural network**. Its output is byte-identical across every supported platform. An analyst replaying a capture offline gets the exact same scores the live system produced.

Rift is **not a mitigation engine**. It does not drop, redirect, throttle, or modify packets. Its alerts are advisory input to a host firewall whose own rules remain authoritative.

## Architecture

```
Network telemetry -> Feature extractor -> Bipolar surprise encoder
    -> SNN tick (leak . afferent . recurrent . threshold . spike)
    -> Class score accumulators -> Per-class confidence scores
```

- **16 features** per tick: volume, rate, protocol mix, SYN/FIN/RST counts, Bloom-cardinality src/dst, packet-size statistics, ACK ratio — 640 input bits
- **8 classes**: normal, SYN flood, UDP amplification, ICMP flood, DNS amplification, FIN scan, HTTP flood, exfiltration — independent class neurons, so two attacks can be flagged at once
- **Fixed-point**: Q16.16 everywhere in the tick path — no floats, no drift, no rounding surprises
- **Deterministic**: same brain + same input = same output on any platform (x86_64, aarch64, Cortex-A, eBPF, SmartNIC ARM)

## What is in this repository

| Directory | What it contains | License |
|---|---|---|
| [`reference/`](reference/) | A complete, unoptimized reference implementation of the engine (~300 lines) | Apache 2.0 |
| [`bin/`](bin/) | Prebuilt production binaries for evaluation (replay, eBPF loader, readers) | Non-commercial |
| [`data/`](data/) | The reference brain and its canonical replay | Reference-brain license |
| [`docs/`](docs/) | The technical specifications | Apache 2.0 |
| [`tests/`](tests/) | The integration test and verification scripts | Apache 2.0 |

The reference implementation demonstrates the algorithm. The production binaries implement the production engineering — the eBPF program, the safety layer, the tick loop — which is what a production license grants. Both produce identical output on the reference brain, verified by CI on every commit.

## Verify in 15 minutes

Three independent paths. All three end in the same SHA-256.

### Path A — Docker (no install)

```bash
docker run --rm ghcr.io/notnullai/rift-replay:latest --verify
```

Expected output ends with:

```
PASS: reference replay matches.
```

### Path B — Prebuilt binary

```bash
git clone https://github.com/notnullai/rift
cd rift
bin/rift_replay-linux-x86_64 \
    data/reflex-brain-rift-demo.rift \
    data/test_input.bin \
    --ticks 10000 \
    --output-binary /tmp/replay.bin
sha256sum /tmp/replay.bin
```

Expected: `c86c5874b8ca8b77d8436b98cdbba79910da20c211496abe41d5a08230dd23e7`
(see `data/reflex-brain-rift-demo-replay.sha256`).

### Path C — Reference implementation (source)

```bash
git clone https://github.com/notnullai/rift
cd rift/reference
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/verify \
    --brain ../data/reflex-brain-rift-demo.rift \
    --input ../data/test_input.bin \
    --ticks 10000 \
    --output-binary /tmp/reference-replay.bin
sha256sum /tmp/reference-replay.bin
```

Expected: the same hash.

The three paths produce the same hash. This is the trust story. Path C uses no prebuilt binary at all — the engine is the ~300-line reference implementation you just built.

## The two verifiable claims

**Bit-exactness.** Path A, Path B, and Path C produce identical replay hashes on every supported platform. The README shows the commands; nothing is claimed that cannot be checked in the same 15 minutes.

**Zero blast radius.** The HAL contract has no `drop`, `redirect`, or `modify` operation. There is no code path from Rift's output to the wire. See `docs/safety_contract.md` and the integration test below.

## The integration test

```bash
sudo ./tests/test_live_pipeline.sh
```

The test attaches the production eBPF program to an isolated loopback interface, generates real traffic, and asserts four things: the program verifies and attaches, the traffic still flows (every packet passed), the engine ticks inside its budget, and the scores are readable. Requires Linux kernel 5.10+ and root.

## Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| x86_64 Linux | Supported | Prebuilt binaries in `bin/` |
| aarch64 Linux | Supported | Prebuilt `rift_replay` in `bin/` |
| eBPF/XDP (Linux 5.10+) | Supported | Production XDP object in `bin/` |
| Cortex-A (bare metal) | Supported | Production runtime (licensed) |
| SmartNIC ARM | Supported | Production runtime (licensed) |

## Documentation

| Document | What it covers |
|---|---|
| `docs/architecture.md` | Big picture, components, data flow |
| `docs/brain_format.md` | The RIFT brain format, field by field |
| `docs/safety_contract.md` | Zero blast radius, heartbeat, integration checklist |
| `docs/training_pipeline.md` | How a brain is produced (the pipeline itself is proprietary) |
| `docs/replay.md` | Replay and the canonical binary |
| `docs/verification.md` | The 15-minute verification, step by step |
| `docs/integration.md` | Deployment, memory model, error codes |

## License

The reference implementation, docs, and tests are Apache 2.0. The production binaries are free for evaluation and non-commercial use (`bin/LICENSE`). The reference brain is distributed under a separate license (`data/LICENSE`). The training pipeline is proprietary. See `LICENSE` and `NOTICE`.

## Contact

- Issues: https://github.com/notnullai/rift/issues
- Security: see `SECURITY.md`
- Commercial licenses: contact@khalm.ai
