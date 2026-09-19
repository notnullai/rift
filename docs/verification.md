# Verification

The 15-minute protocol. Every claim this repository makes can be checked
on your own hardware. Nothing here requires trust: each path ends in the
same SHA-256 hash, which you compare against
`data/reflex-brain-rift-demo-replay.sha256`.

## The two claims

**Bit-exactness.** The same brain and the same input produce the same
10,000 x 8 class scores on every supported platform. Three independent
artifacts — the production binary, the reference implementation, and the
Docker image — all produce the same canonical replay hash.

**Zero blast radius.** The engine has no control path to the wire. The
production eBPF program returns `XDP_PASS` on every packet. The
integration test demonstrates this on an isolated loopback interface.

## Path A — Docker (no install, ~2 minutes)

```bash
docker run --rm ghcr.io/notnullai/rift-replay:latest --verify
```

Expected output ends with:

```
PASS: reference replay matches.
```

The image contains the production binary, the reference brain, and the
reference input. It replays and compares the hash for you.

## Path B — Prebuilt binary (~5 minutes)

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

Compare against `data/reflex-brain-rift-demo-replay.sha256`. The hash must
match.

## Path C — Reference implementation from source (~10 minutes)

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

Same expected hash. Path C uses no prebuilt binary at all — the ~300-line
reference implementation is the engine, and you just built it. This is the
strongest check: if Path A, Path B, and Path C all agree, the bit-exactness
claim is verified on your hardware, from source you can read.

## Verifying zero blast radius

The integration test loads the production eBPF program onto an isolated
loopback interface and asserts that the engine observes traffic without
touching it:

```bash
sudo ./tests/test_live_pipeline.sh
```

Requires Linux kernel 5.10+, root, and a clone of this repository. The
test attaches the XDP program, generates real traffic, confirms the
traffic still flows (every packet passed), and checks the tick-time
budget. See `docs/safety_contract.md` for the full safety contract.

## What the hashes pin

| Artifact | Hash file |
|----------|-----------|
| Reference brain | `data/reflex-brain-rift-demo.sha256` |
| Canonical 10,000-tick replay | `data/reflex-brain-rift-demo-replay.sha256` |

The replay hash is the reproducibility token. It is the same across
x86_64, aarch64, Cortex-A, eBPF, and SmartNIC ARM.
