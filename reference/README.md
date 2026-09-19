# RIFT Reference Implementation

This directory contains a complete, unoptimized reference implementation of
the RIFT engine. It exists so that anyone can read exactly what the engine
does, build it, and verify that its output is byte-identical to the
canonical reference replay — without trusting any binary.

## What it demonstrates

RIFT is a Q16.16 fixed-point spiking neural network that classifies network
traffic windows. Each tick:

1. packs the previous tick's spikes into two bit vectors (excitatory and
   inhibitory);
2. for every neuron: leaks the membrane potential, adds the afferent input
   (popcount of input-AND-mask, twice: excitatory and inhibitory), adds the
   recurrent input (four popcounts covering every signed-spike x
   signed-weight combination), and spikes +1 / -1 if the potential crosses
   the neuron's threshold, resetting it to zero;
3. updates each class neuron's rolling 512-tick spike history and recomputes
   its score as (spikes in window << 16) / window.

Every value in the tick path is an integer. There is no floating point and
no randomness, which is what makes the output byte-identical across
compilers and architectures.

## What it does NOT contain

The reference demonstrates the algorithm, not the production engineering.
It has no eBPF program, no safety layer, no HAL, no lazy ticking, no
predictor, no arena allocation, and no optimization. The production runtime
adds all of these on top of the same arithmetic.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Only a C++11 compiler and the standard library are required. There are no
dependencies on the rest of this repository.

## Verify

```bash
./build/verify \
    --brain ../data/reflex-brain-rift-demo.rift \
    --input ../data/test_input.bin \
    --ticks 10000 \
    --output-binary /tmp/reference-replay.bin

sha256sum /tmp/reference-replay.bin
```

The hash must match `data/reflex-brain-rift-demo-replay.sha256`. If it
does, this implementation produces the same 10,000 x 8 class scores as the
canonical reference replay on your machine.

## How to read it

Start with `rift_reference.h` (the interface), then `rift_reference.cpp`
(the tick). `rift_brain_reference.h` documents the brain file layout;
`rift_brain_reference.cpp` walks the four ternary weight masks out of the
file. `rift_fixed_point.h` holds the Q16.16 primitives — it is shared
verbatim with the production runtime, because the arithmetic must be
identical. `verify.cpp` is the command-line harness.

The whole engine is about 300 lines, comments included. A careful reader
can absorb it in one sitting.

## Relationship to the production binaries

The production runtime licenses the engineering around this algorithm: the
eBPF implementation, the safety layer, the tick loop, and the platform
integrations. Both produce identical scores on the reference brain, and
that identity is verified in CI on every commit.
