#pragma once

#include <cstdint>
#include <cstddef>

// Reference engine interface.
//
// The reference implementation demonstrates the RIFT tick algorithm. It is
// deliberately simple: plain C++03, heap allocation via malloc, no arena,
// no safety layer, no HAL, no predictor. It produces byte-identical class
// scores to the production runtime on the same brain and input.

typedef struct rift_ref_engine rift_ref_engine;

// Geometry, read from the brain header.
typedef struct rift_ref_geometry {
    uint32_t input_bits;
    uint32_t hidden_neurons;
    uint32_t class_neurons;
    uint32_t feature_count;
    uint32_t input_words;
    uint32_t hidden_words;
    uint32_t class_words;
    int32_t  fire_threshold;
    uint32_t leak_q16;
    uint32_t score_window;
} rift_ref_geometry;

// Loads a brain from memory. The brain bytes must remain valid for the
// lifetime of the engine (the engine keeps pointers into them).
// Returns a heap-allocated engine, or NULL on error.
rift_ref_engine* rift_ref_load(const uint8_t* brain_data, size_t brain_size,
                                rift_ref_geometry* geom_out);

// Frees the engine.
void rift_ref_free(rift_ref_engine* e);

// Loads the input word frame for the next tick (input_words x uint32).
void rift_ref_set_input(rift_ref_engine* e, const uint32_t* words);

// Runs one tick: leak, afferent, recurrent, threshold, spike, score update.
void rift_ref_step(rift_ref_engine* e);

// Copies the current class scores (Q16.16) into out[class_neurons].
void rift_ref_get_scores(const rift_ref_engine* e, int32_t* out);

// Resets all neuron and score state.
void rift_ref_reset(rift_ref_engine* e);

// Number of input words per frame.
uint32_t rift_ref_input_words(const rift_ref_engine* e);
