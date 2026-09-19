cmake_minimum_required(VERSION 3.16)
project(rift_reference CXX)

# The reference implementation is standalone: it depends only on the C++
# standard library. Build and verify:
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/verify --brain ../data/reflex-brain-rift-demo.rift \
#                  --input ../data/test_input.bin --ticks 10000 \
#                  --output-binary /tmp/reference-replay.bin
#   sha256sum /tmp/reference-replay.bin   # must match the published hash

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(verify
    verify.cpp
    rift_reference.cpp
    rift_brain_reference.cpp
)
