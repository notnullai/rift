# RIFT Reference Data

This directory contains the reference brain and its canonical verification
artifacts. Every value here is reproducible: the brain is a deterministic
function of its training seed, and the replay is a deterministic function
of the brain and the input.

## Files

| File | What it is |
|------|-----------|
| `reflex-brain-rift-demo.rift` | Reference brain: 512 hidden neurons, 8 class neurons, 16 features (640 input bits), score window 512. ~150 KB. |
| `reflex-brain-rift-demo.sha256` | SHA-256 of the brain file. |
| `test_input.bin` | Reference input: 10,000 tick windows, 20 little-endian uint32 words each (640 input bits). 800,000 bytes. |
| `reflex-brain-rift-demo-replay.bin` | Canonical replay: header (magic `"RIFT"`, version 1, record count, class count, score window) followed by 10,000 x 8 Q16.16 class scores, little-endian. |
| `reflex-brain-rift-demo-replay.sha256` | SHA-256 of the replay binary. |

## How to verify

Three paths produce the same hash. See `docs/verification.md` for the full
protocol; the shortest is:

```bash
# 1. Replay the reference input through the reference brain
bin/rift_replay-linux-x86_64 \
    data/reflex-brain-rift-demo.rift \
    data/test_input.bin \
    --ticks 10000 \
    --output-binary /tmp/replay.bin

# 2. Compare the hash
sha256sum /tmp/replay.bin
cat data/reflex-brain-rift-demo-replay.sha256
```

The hash must match. This proves the engine produces byte-identical output
on your machine, which is the foundation of the cross-platform
bit-exactness claim.

You can also verify with the standalone reference implementation, which
has no dependency on any prebuilt binary:

```bash
cd reference
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/verify --brain ../data/reflex-brain-rift-demo.rift \
    --input ../data/test_input.bin --ticks 10000 \
    --output-binary /tmp/reference-replay.bin
sha256sum /tmp/reference-replay.bin    # same hash
```

## License

See `LICENSE` in this directory. The reference brain is provided for
demonstration and verification, not for retraining or production use.
