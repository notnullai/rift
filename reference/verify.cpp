// verify — runs the reference engine against a brain and an input stream,
// and writes the canonical replay binary.
//
// Usage:
//   verify --brain BRAIN --input INPUT --ticks N
//          [--output-binary PATH] [--output-csv PATH]
//
// The input file is a stream of frames; each frame is input_words x uint32,
// little-endian. The frames are replayed in order (cycling if --ticks
// exceeds the number of frames), one frame per tick.
//
// The canonical binary is:
//   4 bytes  magic "RIFT"
//   4 bytes  version (1)
//   8 bytes  record count (uint64)
//   8 bytes  class count (uint64)
//   8 bytes  score window (uint64)
//   then, per tick: class_count x int32 Q16.16 scores, little-endian
//
// Its SHA-256 must match the published reference hash.

#include "rift_reference.h"
#include "rift_brain_reference.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma pack(push, 1)
typedef struct {
    char     magic[4];
    uint32_t version;
    uint64_t num_records;
    uint64_t class_count;
    uint64_t score_window;
} replay_binary_header;
#pragma pack(pop)

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s --brain BRAIN --input INPUT [--ticks N]\n"
        "              [--output-binary PATH] [--output-csv PATH]\n", prog);
}

int main(int argc, char** argv) {
    const char* brain_path = NULL;
    const char* input_path = NULL;
    const char* binary_path = NULL;
    const char* csv_path = NULL;
    uint64_t max_ticks = 10000;

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--brain") && i + 1 < argc) brain_path = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input_path = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) max_ticks = (uint64_t)atoll(argv[++i]);
        else if (!strcmp(argv[i], "--output-binary") && i + 1 < argc) binary_path = argv[++i];
        else if (!strcmp(argv[i], "--output-csv") && i + 1 < argc) csv_path = argv[++i];
        else { print_usage(argv[0]); return 1; }
    }
    if (!brain_path || !input_path) { print_usage(argv[0]); return 1; }

    // Read the brain file.
    FILE* bf = fopen(brain_path, "rb");
    if (!bf) { fprintf(stderr, "cannot open brain: %s\n", brain_path); return 1; }
    fseek(bf, 0, SEEK_END);
    long bsize = ftell(bf);
    fseek(bf, 0, SEEK_SET);
    uint8_t* brain = (uint8_t*)malloc((size_t)bsize);
    if (fread(brain, 1, (size_t)bsize, bf) != (size_t)bsize) {
        fprintf(stderr, "brain read failed\n");
        fclose(bf);
        return 1;
    }
    fclose(bf);

    // Read the input frames.
    FILE* inf = fopen(input_path, "rb");
    if (!inf) { fprintf(stderr, "cannot open input: %s\n", input_path); return 1; }
    fseek(inf, 0, SEEK_END);
    long isize = ftell(inf);
    fseek(inf, 0, SEEK_SET);
    uint8_t* input = (uint8_t*)malloc((size_t)isize);
    if (fread(input, 1, (size_t)isize, inf) != (size_t)isize) {
        fprintf(stderr, "input read failed\n");
        fclose(inf);
        return 1;
    }
    fclose(inf);

    // Load the engine.
    rift_ref_geometry geom;
    rift_ref_engine* e = rift_ref_load(brain, (size_t)bsize, &geom);
    if (!e) { fprintf(stderr, "brain load failed\n"); return 1; }

    const uint32_t frame_words = geom.input_words;
    const size_t   frame_size  = (size_t)frame_words * sizeof(uint32_t);
    const uint64_t num_frames  = frame_size ? (uint64_t)isize / frame_size : 0;
    if (num_frames == 0) { fprintf(stderr, "input too small\n"); return 1; }

    FILE* bfout = NULL;
    FILE* csout = NULL;
    replay_binary_header hdr;
    memcpy(hdr.magic, "RIFT", 4);
    hdr.version = 1;
    hdr.num_records = max_ticks;
    hdr.class_count = geom.class_neurons;
    hdr.score_window = geom.score_window;

    if (binary_path) {
        bfout = fopen(binary_path, "wb");
        if (!bfout) { fprintf(stderr, "cannot open binary output\n"); return 1; }
        fwrite(&hdr, sizeof(hdr), 1, bfout);
    }
    if (csv_path) {
        csout = fopen(csv_path, "w");
        if (!csout) { fprintf(stderr, "cannot open csv output\n"); return 1; }
        fprintf(csout, "tick");
        for (uint32_t c = 0; c < geom.class_neurons; ++c)
            fprintf(csout, ",class_%u", c);
        fprintf(csout, "\n");
    }

    // Replay.
    rift_ref_reset(e);
    int32_t* scores = (int32_t*)malloc((size_t)geom.class_neurons * sizeof(int32_t));
    for (uint64_t tick = 0; tick < max_ticks; ++tick) {
        uint64_t frame = tick % num_frames;
        rift_ref_set_input(e, (const uint32_t*)(input + frame * frame_size));
        rift_ref_step(e);
        rift_ref_get_scores(e, scores);

        if (bfout)
            fwrite(scores, sizeof(int32_t), (size_t)geom.class_neurons, bfout);
        if (csout) {
            fprintf(csout, "%llu", (unsigned long long)tick);
            for (uint32_t c = 0; c < geom.class_neurons; ++c)
                fprintf(csout, ",%.6f", (double)scores[c] / 65536.0);
            fprintf(csout, "\n");
        }
    }

    if (bfout) fclose(bfout);
    if (csout) fclose(csout);

    printf("replayed %llu ticks, %u classes\n",
           (unsigned long long)max_ticks, geom.class_neurons);
    for (uint32_t c = 0; c < geom.class_neurons; ++c)
        printf("  class %u final score: %.6f\n", c, (double)scores[c] / 65536.0);

    free(scores);
    free(input);
    free(brain);
    rift_ref_free(e);
    return 0;
}
