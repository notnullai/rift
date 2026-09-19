// RIFT reference engine.
//
// This is the algorithm in its simplest readable form. Every computation
// here matches the production runtime exactly; the production engine adds
// arena allocation, validation, safety and platform integration on top of
// the same math.
//
// A tick does the following:
//
//   1. Pack the previous tick's spikes into two bit vectors (excitatory /
//      inhibitory). Class neurons are masked out of the recurrent input:
//      the classes are outputs, not state.
//   2. For each neuron:
//        - leak the membrane potential (sign-corrected truncating shift)
//        - add the afferent input: popcount of (input AND exc mask) minus
//          popcount of (input AND inh mask)
//        - add the recurrent input: for each of the four (spike, weight)
//          sign combinations, popcount of the AND of the two vectors
//        - if V >= threshold, spike +1 and reset V to 0; if V <= -threshold,
//          spike -1 and reset V to 0
//   3. For each class neuron, update the rolling 512-tick spike history
//      bitmask and recompute the score: (spikes in window << 16) / window.
//
// All arithmetic is integer. There is no floating point anywhere in the
// tick path, which is what makes the output byte-identical across
// compilers and architectures.

#include "rift_reference.h"
#include "rift_brain_reference.h"
#include "rift_fixed_point.h"

#include <cstdlib>
#include <cstring>

struct rift_ref_engine {
    rift_ref_geometry geom;

    // Weight masks: point into the brain file bytes.
    const uint32_t* aff_exc;
    const uint32_t* aff_inh;
    const uint32_t* rec_exc;
    const uint32_t* rec_inh;
    const int32_t*  threshold;

    // Mutable state.
    int32_t*  V;              // membrane potentials
    int8_t*   spike;          // previous tick spikes (+1 / 0 / -1)
    int8_t*   next;           // current tick spikes
    uint32_t* s_exc;          // packed previous-tick excitatory spikes
    uint32_t* s_inh;          // packed previous-tick inhibitory spikes
    uint32_t* input;          // encoded input for this tick

    uint32_t* score_history;  // rolling bitmask per class neuron
    uint32_t  score_head;     // position in the rolling window
    int32_t*  scores_q16;     // current class scores
};

static inline int ref_popcount(uint32_t v) {
    // Hardware popcount where available; the count itself is what matters.
#if defined(_MSC_VER)
    return __popcnt(v);
#else
    return __builtin_popcount(v);
#endif
}

rift_ref_engine* rift_ref_load(const uint8_t* brain_data, size_t brain_size,
                                rift_ref_geometry* geom_out) {
    if (!brain_data || brain_size < sizeof(rift_ref_brain_header) || !geom_out)
        return NULL;

    const rift_ref_brain_header* hdr = (const rift_ref_brain_header*)brain_data;
    if (memcmp(hdr->magic, RIFT_REF_BRAIN_MAGIC, 8) != 0) return NULL;
    if (hdr->version != RIFT_REF_BRAIN_VERSION) return NULL;

    rift_ref_brain_body body;
    if (rift_ref_brain_parse(hdr, brain_data, &body) != 0) return NULL;

    const uint32_t hidden = hdr->hidden_neurons;
    const uint32_t hid_w  = hdr->hidden_words;
    const uint32_t in_w   = hdr->input_words;
    const uint32_t cls_n  = hdr->class_neurons;
    const uint32_t cls_w  = hdr->class_words;

    rift_ref_engine* e = (rift_ref_engine*)calloc(1, sizeof(rift_ref_engine));
    if (!e) return NULL;

    e->geom.input_bits     = hdr->input_bits;
    e->geom.hidden_neurons = hidden;
    e->geom.class_neurons  = cls_n;
    e->geom.feature_count  = hdr->feature_count;
    e->geom.input_words    = in_w;
    e->geom.hidden_words   = hid_w;
    e->geom.class_words    = cls_w;
    e->geom.fire_threshold = hdr->fire_threshold;
    e->geom.leak_q16       = hdr->leak_q16;
    e->geom.score_window   = hdr->score_window;

    e->aff_exc  = body.aff_exc;
    e->aff_inh  = body.aff_inh;
    e->rec_exc  = body.rec_exc;
    e->rec_inh  = body.rec_inh;
    e->threshold = body.threshold;

    e->V       = (int32_t*) calloc(hidden, sizeof(int32_t));
    e->spike   = (int8_t*)  calloc(hidden, sizeof(int8_t));
    e->next    = (int8_t*)  calloc(hidden, sizeof(int8_t));
    e->s_exc   = (uint32_t*)calloc(hid_w, sizeof(uint32_t));
    e->s_inh   = (uint32_t*)calloc(hid_w, sizeof(uint32_t));
    e->input   = (uint32_t*)calloc(in_w, sizeof(uint32_t));
    e->score_history = (uint32_t*)calloc((size_t)cls_n * cls_w, sizeof(uint32_t));
    e->scores_q16    = (int32_t*) calloc(cls_n, sizeof(int32_t));
    e->score_head = 0;

    if (!e->V || !e->spike || !e->next || !e->s_exc || !e->s_inh ||
        !e->input || !e->score_history || !e->scores_q16) {
        rift_ref_free(e);
        return NULL;
    }

    *geom_out = e->geom;
    return e;
}

void rift_ref_free(rift_ref_engine* e) {
    if (!e) return;
    free(e->V);
    free(e->spike);
    free(e->next);
    free(e->s_exc);
    free(e->s_inh);
    free(e->input);
    free(e->score_history);
    free(e->scores_q16);
    free(e);
}

void rift_ref_set_input(rift_ref_engine* e, const uint32_t* words) {
    memcpy(e->input, words, (size_t)e->geom.input_words * sizeof(uint32_t));
}

void rift_ref_step(rift_ref_engine* e) {
    const uint32_t hidden = e->geom.hidden_neurons;
    const uint32_t cls_n  = e->geom.class_neurons;
    const uint32_t in_w   = e->geom.input_words;
    const uint32_t hid_w  = e->geom.hidden_words;

    // ---- 1. Pack previous-tick spikes into s_exc / s_inh.
    // Class neurons (indices 0 .. class_neurons-1) are outputs; their spikes
    // never re-enter the network. Clear their bits.
    for (uint32_t w = 0; w < hid_w; ++w) {
        uint32_t exc = 0, inh = 0;
        for (uint32_t b = 0; b < 32; ++b) {
            uint32_t n = w * 32 + b;
            if (n >= hidden) break;
            int8_t sp = e->spike[n];
            if (sp == 1)  exc |= (1u << b);
            else if (sp == -1) inh |= (1u << b);
        }
        uint32_t lo = w * 32;
        if (lo + 32 <= cls_n) {
            exc = 0; inh = 0;
        } else if (lo < cls_n) {
            uint32_t keep = ~((1u << (cls_n - lo)) - 1u);
            exc &= keep; inh &= keep;
        }
        e->s_exc[w] = exc;
        e->s_inh[w] = inh;
    }

    // ---- 2. Leaky integrate and fire, neuron by neuron.
    for (uint32_t n = 0; n < hidden; ++n) {
        // Leak: V <- V * leak_q16 / 65536, truncating toward zero.
        e->V[n] = leak_apply(e->V[n], e->geom.leak_q16);

        // Afferent: each input line contributes +1 if the input bit is set
        // and the neuron's excitatory mask bit is set, -1 for inhibitory.
        const uint32_t* ae = &e->aff_exc[(size_t)n * in_w];
        const uint32_t* ai = &e->aff_inh[(size_t)n * in_w];
        int match = 0;
        for (uint32_t w = 0; w < in_w; ++w) {
            match += ref_popcount(e->input[w] & ae[w]);
            match -= ref_popcount(e->input[w] & ai[w]);
        }
        e->V[n] += match;

        // Recurrent: signed spike (+1/-1) times signed weight (+1/-1).
        // The four sign combinations add or subtract by popcount.
        const uint32_t* re = &e->rec_exc[(size_t)n * hid_w];
        const uint32_t* ri = &e->rec_inh[(size_t)n * hid_w];
        for (uint32_t w = 0; w < hid_w; ++w) {
            uint32_t se = e->s_exc[w];
            uint32_t si = e->s_inh[w];
            e->V[n] += ref_popcount(se & re[w]);   // (+spike, +weight)
            e->V[n] += ref_popcount(si & ri[w]);   // (-spike, -weight)
            e->V[n] -= ref_popcount(se & ri[w]);   // (+spike, -weight)
            e->V[n] -= ref_popcount(si & re[w]);   // (-spike, +weight)
        }

        // Threshold. Class neurons use the brain's fire_threshold; hidden
        // neurons use their per-neuron threshold. Spiking resets V to 0.
        int32_t th = (n < cls_n) ? e->geom.fire_threshold : e->threshold[n];
        int8_t sp = 0;
        if (e->V[n] >= th)       { sp = 1;  e->V[n] = 0; }
        else if (e->V[n] <= -th) { sp = -1; e->V[n] = 0; }
        e->next[n] = sp;
    }

    // ---- 3. Class scores: a rolling window of spike bits.
    // Only positive class spikes count (class scores are firing rates).
    for (uint32_t c = 0; c < cls_n; ++c) {
        uint32_t* hist = &e->score_history[(size_t)c * e->geom.class_words];
        uint32_t word_idx = e->score_head >> 5;
        uint32_t bit_idx  = e->score_head & 31;

        hist[word_idx] &= ~(1u << bit_idx);        // drop the oldest bit
        if (e->next[c] == 1)
            hist[word_idx] |= (1u << bit_idx);     // record this tick's spike

        int spike_sum = 0;
        for (uint32_t w = 0; w < e->geom.class_words; ++w)
            spike_sum += ref_popcount(hist[w]);

        e->scores_q16[c] = (int32_t)(((int64_t)spike_sum << 16)
                                     / e->geom.score_window);
    }
    e->score_head = (e->score_head + 1) % e->geom.score_window;

    // ---- Commit this tick's spikes as last-tick spikes.
    memcpy(e->spike, e->next, (size_t)hidden * sizeof(int8_t));
}

void rift_ref_get_scores(const rift_ref_engine* e, int32_t* out) {
    memcpy(out, e->scores_q16, (size_t)e->geom.class_neurons * sizeof(int32_t));
}

void rift_ref_reset(rift_ref_engine* e) {
    memset(e->V, 0, (size_t)e->geom.hidden_neurons * sizeof(int32_t));
    memset(e->spike, 0, (size_t)e->geom.hidden_neurons * sizeof(int8_t));
    memset(e->next, 0, (size_t)e->geom.hidden_neurons * sizeof(int8_t));
    memset(e->s_exc, 0, (size_t)e->geom.hidden_words * sizeof(uint32_t));
    memset(e->s_inh, 0, (size_t)e->geom.hidden_words * sizeof(uint32_t));
    memset(e->input, 0, (size_t)e->geom.input_words * sizeof(uint32_t));
    memset(e->score_history, 0,
           (size_t)e->geom.class_neurons * e->geom.class_words * sizeof(uint32_t));
    memset(e->scores_q16, 0, (size_t)e->geom.class_neurons * sizeof(int32_t));
    e->score_head = 0;
}

uint32_t rift_ref_input_words(const rift_ref_engine* e) {
    return e->geom.input_words;
}
