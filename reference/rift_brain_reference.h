#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

// RIFT brain format (v2), little-endian.
//
// This is a simplified reader for the reference implementation. It reads
// the geometry and the four ternary weight masks; it does not validate the
// encoder geometry hash (the production loader does).
//
// Layout on disk:
//   header (96 bytes, packed)
//   encoder geometry block (648 bytes: n_features u32 + n_absolute_channels
//     u32 + 32 x 20-byte feature descriptors)
//   afferent excitatory masks   hidden_neurons x input_words x u32
//   afferent inhibitory masks   hidden_neurons x input_words x u32
//   recurrent excitatory masks  hidden_neurons x hidden_words x u32
//   recurrent inhibitory masks  hidden_neurons x hidden_words x u32
//   per-neuron thresholds       hidden_neurons x i32
//   predictor params            2 x feature_count x i32   (unused by the engine)

#define RIFT_REF_BRAIN_MAGIC "RIFTBRA2"
#define RIFT_REF_BRAIN_VERSION 2
#define RIFT_REF_MAX_FEATURES 32
#define RIFT_REF_ENCODER_GEOM_BLOCK_SIZE 648

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

typedef struct rift_ref_brain_header {
    char     magic[8];               // "RIFTBRA2"
    uint32_t version;                // 2
    uint32_t flags;                  // 0
    uint32_t input_bits;             // multiple of 32
    uint32_t hidden_neurons;         // multiple of 32
    uint32_t class_neurons;
    uint32_t feature_count;
    uint32_t input_words;            // input_bits / 32
    uint32_t hidden_words;           // hidden_neurons / 32
    uint32_t class_words;            // (score_window + 31) / 32
    int32_t  fire_threshold;         // class-neuron spike threshold
    uint32_t leak_q16;               // membrane leak, Q16.16
    uint32_t score_window;           // rolling score window in ticks
    uint32_t predictor_params_count; // 2 x feature_count
    uint64_t encoder_geometry_hash;  // ignored by the reference reader
    uint32_t reserved[7];
#if defined(__GNUC__) || defined(__clang__)
} __attribute__((packed)) rift_ref_brain_header;
#else
} rift_ref_brain_header;
#endif

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

// Parsed body: pointers into the brain file bytes. The file must stay alive
// for the lifetime of the engine.
typedef struct rift_ref_brain_body {
    const uint32_t* aff_exc;   // [neuron][input_word]
    const uint32_t* aff_inh;
    const uint32_t* rec_exc;   // [neuron][hidden_word]
    const uint32_t* rec_inh;
    const int32_t*  threshold; // per-neuron threshold (hidden neurons)
} rift_ref_brain_body;

// Computes the body pointers from a loaded brain. Returns 0 on success.
int rift_ref_brain_parse(const rift_ref_brain_header* hdr,
                         const uint8_t* brain_data,
                         rift_ref_brain_body* body);
