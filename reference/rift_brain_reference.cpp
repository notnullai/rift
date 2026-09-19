#include "rift_brain_reference.h"

int rift_ref_brain_parse(const rift_ref_brain_header* hdr,
                         const uint8_t* brain_data,
                         rift_ref_brain_body* body) {
    if (!hdr || !brain_data || !body) return -1;

    // Body starts after the header and the fixed-size encoder geometry
    // block. The reference reader skips the geometry block entirely.
    const uint8_t* p = brain_data + sizeof(rift_ref_brain_header)
                       + RIFT_REF_ENCODER_GEOM_BLOCK_SIZE;

    const size_t aff_words = (size_t)hdr->hidden_neurons * hdr->input_words;
    const size_t rec_words = (size_t)hdr->hidden_neurons * hdr->hidden_words;

    body->aff_exc = (const uint32_t*)p;
    p += aff_words * sizeof(uint32_t);

    body->aff_inh = (const uint32_t*)p;
    p += aff_words * sizeof(uint32_t);

    body->rec_exc = (const uint32_t*)p;
    p += rec_words * sizeof(uint32_t);

    body->rec_inh = (const uint32_t*)p;
    p += rec_words * sizeof(uint32_t);

    body->threshold = (const int32_t*)p;
    p += (size_t)hdr->hidden_neurons * sizeof(int32_t);

    // Predictor params follow; the engine does not use them.
    return 0;
}
