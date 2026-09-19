# RIFT Integration Guide

## Quick Start

### 1. Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### 2. Run Tests
```bash
ctest --test-dir build -C Debug --output-on-failure
```

### 3. Load a Brain
```cpp
#include "rift/rift.h"
#include <stdio.h>

int main() {
    // Read brain file
    FILE* f = fopen("model.rift", "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* brain_data = malloc(size);
    fread(brain_data, 1, size, f);
    fclose(f);

    // Get geometry and required memory
    rift_geometry geom;
    size_t arena_size;
    rift_status st = rift_get_required_memory(brain_data, size, &geom, &arena_size);
    if (st != RIFT_OK) { /* handle error */ }

    // Allocate aligned arena
    void* arena = malloc(arena_size);

    // Load brain
    rift_engine* engine;
    st = rift_load(brain_data, size, arena, arena_size, &engine);
    if (st != RIFT_OK) { /* handle error */ }

    // Engine is ready — rift_reset(engine) to clean up
}
```

### 4. Process Frames
```cpp
// Each tick: set input, step, get scores
uint32_t input[20];  // 640 bits packed into 20 words (16 features × 40 bits)
int32_t scores[8];   // class scores (Q16.16)

rift_set_input(engine, input);
rift_step(engine);
rift_get_scores(engine, scores);

// scores[i] ranges: 0 (no activity) to 65536 (sustained activity)
```

### 5. Reset for Reuse
```cpp
rift_reset(engine);
// Engine ready for new session — no reallocation needed
```

## Memory Model
- **Brain data**: Loaded once, kept alive during engine lifetime
- **Arena**: Caller-allocated, engine-internal state lives here
- **No malloc after init**: All mutable state in arena

## Error Codes
| Code | Name | Meaning |
|------|------|---------|
| 0 | RIFT_OK | Success |
| -1 | RIFT_ERR_ARG | Null pointer, index out of range |
| -2 | RIFT_ERR_FORMAT | Brain file format violation |
| -3 | RIFT_ERR_GEOMETRY | Shape violation |
| -4 | RIFT_ERR_ARENA | Arena too small or unaligned |
| -5 | RIFT_ERR_GEOM_HASH | Encoder geometry hash mismatch |
| -6 | RIFT_ERR_SAFETY | Safety layer in fault state |

## Integration Checklist
- [ ] Brain file loaded and validated
- [ ] Arena allocated (8-byte aligned)
- [ ] Input frames match brain's `input_words` count
- [ ] Tick budget defined (default 1000us for 1kHz)
- [ ] Safety layer configured (if using heartbeat monitoring)
- [ ] Scores interpreted as Q16.16 (divide by 65536.0 for float)

## HAL Implementation
For custom platforms, implement the HAL interface:
```cpp
#include "rift/rift_hal.h"

rift_hal hal = {
    .read_telemetry_frame = my_read_frame,
    .signal_anomaly = my_signal_anomaly,
    .now_us = my_timestamp,
    .kick_hardware_watchdog = my_watchdog,
    .log = my_log
};
```

## Safety Integration
```cpp
#include "rift/rift_safety.h"

rift_safety_config scfg = {
    .tick_budget_us = 1000,
    .watchdog_misses = 100
};

rift_safety* safety;
uint8_t safety_arena[1024];
rift_safety_create(&scfg, safety_arena, sizeof(safety_arena), &safety);

// In tick loop:
rift_safety_pre_tick(safety, engine);
rift_step(engine);
rift_safety_heartbeat(safety, tick_count);
rift_safety_post_tick(safety, &hal);

if (!rift_safety_ok(safety)) {
    // Fault — engine may be stalled, use fallback
}
```
