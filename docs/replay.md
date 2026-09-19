# RIFT Replay & Determinism

## Overview

RIFT replay loads a trained brain, processes input frames, and produces
deterministic, byte-identical output across all supported platforms. The
same brain and the same input produce the same scores — on a developer's
laptop, on a production host, and on the reference implementation.

## Replay Binary Format

```
Header (32 bytes):
  [0:3]   Magic: "RIFT"
  [4:7]   Version: uint32_t (1)
  [8:15]  Number of records: uint64_t
  [16:23] Class count: uint64_t
  [24:31] Score window: uint64_t

Body:
  Per tick: class_count x int32_t scores (Q16.16)
```

## SHA-256 Hash

The canonical binary's SHA-256 hash is the reproducibility token. Same
brain + same input -> same hash, regardless of platform, compiler, or
architecture. The canonical hash for the shipped reference artifacts is in
`data/reflex-brain-rift-demo-replay.sha256`.

## Tools

### `rift_replay`

The production replay CLI ships as a prebuilt binary in `bin/`. It replays
an input through a brain and writes the canonical binary:

```bash
bin/rift_replay-linux-x86_64 \
    data/reflex-brain-rift-demo.rift \
    data/test_input.bin \
    --ticks 10000 \
    --output-binary /tmp/replay.bin

sha256sum /tmp/replay.bin
```

The hash must match `data/reflex-brain-rift-demo-replay.sha256`.

### `verify`

The reference implementation ships its own harness, `reference/verify`,
which reads the same brain and input and writes the same canonical binary
format:

```bash
cd reference
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/verify \
    --brain ../data/reflex-brain-rift-demo.rift \
    --input ../data/test_input.bin \
    --ticks 10000 \
    --output-binary /tmp/reference-replay.bin

sha256sum /tmp/reference-replay.bin
```

Same hash. Both tools are compared against the same canonical replay in CI
on every commit.

## Verification in CI

| Check | What it proves |
|-------|----------------|
| Reference `verify` vs canonical replay | The open reference implementation is byte-identical to the canonical output |
| `rift_replay` vs canonical replay | The production binary is byte-identical to the canonical output |
| Reference vs production on the same brain | The two implementations agree tick for tick |

All three converge on `data/reflex-brain-rift-demo-replay.sha256`.

## Platform Notes

- **x86_64 Linux**: prebuilt binary in `bin/`, built with `-O2 -fno-exceptions -fno-rtti`
- **aarch64 Linux**: prebuilt binary in `bin/`, cross-compiled with the same flags
- **eBPF**: the production XDP program ships as a compiled object in `bin/`; its scores are byte-identical to the runtime
- **Cortex-A / SmartNIC ARM**: supported by the production runtime; the same replay hash holds

## Usage

`rift_replay` is a command-line tool:

```bash
bin/rift_replay-linux-x86_64 BRAIN.rift INPUT.bin \
    [--ticks N] [--output-binary OUT.bin] [--output-csv OUT.csv]
```

The reference implementation exposes the same operation through `verify`
(see `reference/README.md`). The engine interface itself is documented in
`docs/brain_format.md` and demonstrated line by line in
`reference/rift_reference.cpp`.
