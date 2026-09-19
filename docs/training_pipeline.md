# RIFT Training Pipeline

This document describes what a RIFT brain is and how one is produced, at
the level needed to understand the shipped artifacts. The training
pipeline itself is proprietary and is not distributed.

## What a brain is

A RIFT brain is a frozen, fully-quantized spiking neural network. It
contains:

- **Geometry**: input bits, hidden neurons, class neurons, score window —
  all stored in the brain header, with no fixed caps.
- **Weights**: four ternary masks per neuron (-1 / 0 / +1) — afferent
  excitatory and inhibitory, recurrent excitatory and inhibitory — packed
  as bitmasks.
- **Thresholds**: one per neuron, in fixed-point units.
- **Encoder geometry**: per-feature surprise ranges, dead zones, and
  predictor alphas, plus an `encoder_geometry_hash` that binds the brain
  to the encoder configuration it was trained with.

The runtime refuses to load a brain whose geometry hash does not match its
encoder. This prevents config-drift deployments: the brain and the encoder
it was trained with are one artifact.

## How a brain is produced

Training is quantization-aware and supervised:

1. **Feature extraction.** Network captures are parsed into 16 per-window
   features: volume, rate, protocol mix, SYN/FIN/RST/ACK counts,
   Bloom-cardinality of source and destination IPs, packet-size
   statistics, and port cardinality.
2. **Surprise encoding.** A first-order EWMA predictor runs ahead of each
   feature. The surprise (observed minus predicted) is encoded as a
   bipolar thermometer: a perfect prediction contributes no input bits;
   deviation lights up bits proportional to surprise.
3. **Quantization-aware forward pass.** The spiking network ticks in
   Q16.16 fixed point, exactly as the deployed runtime will — leak,
   afferent, recurrent, threshold, spike, class score accumulation. The
   forward pass is bit-exact with the deployed engine.
4. **Surrogate-gradient backward pass.** A surrogate gradient flows back
   through the spike threshold; float shadow weights accumulate gradients
   under Adam.
5. **Ternary quantization.** Shadow weights are quantized to -1 / 0 / +1
   with a dead zone around zero, packed into the brain's bitmask
   representation, and exported with the encoder geometry.

Because the forward pass is the same fixed-point arithmetic as the runtime,
the trained brain's scores are byte-identical to the deployed engine's
scores on every training window. That identity is verified before any
brain ships.

## The reference brain

`data/reflex-brain-rift-demo.rift` is the reference brain: 512 hidden
neurons, 8 class neurons, 16 features (640 input bits), score window 512.
It is the brain every verification path in this repository replays, and
`data/reflex-brain-rift-demo-replay.bin` is its canonical 10,000-tick
output.

The reference brain is a deterministic function of its training seed and
data. It is provided for demonstration and verification — see
`data/LICENSE` for what you may and may not do with it. Production brains
are trained against a deployment's own traffic.
