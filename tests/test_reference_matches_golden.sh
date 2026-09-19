#!/bin/bash
# test_reference_matches_golden.sh — Reference implementation vs canonical replay
#
# Builds the reference implementation from source, runs it against the
# reference brain and input, and compares the output hash against the
# canonical replay. If the production binary is present, it is compared
# against the same canonical hash as well.
#
# Exit codes: 0 = pass, 1 = fail
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BRAIN="data/reflex-brain-rift-demo.rift"
INPUT="data/test_input.bin"
EXPECTED=$(cat data/reflex-brain-rift-demo-replay.sha256 | awk '{print $1}')
TMPDIR_BIN=$(mktemp -d)
trap 'rm -rf "$TMPDIR_BIN"' EXIT

echo "=== Reference implementation vs canonical replay ==="

echo "  Building reference implementation..."
cmake -B reference/build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build reference/build -j > /dev/null

echo "  Running reference verify (10,000 ticks)..."
reference/build/verify \
    --brain "$BRAIN" \
    --input "$INPUT" \
    --ticks 10000 \
    --output-binary "$TMPDIR_BIN/reference-replay.bin"

REF_HASH=$(sha256sum "$TMPDIR_BIN/reference-replay.bin" | awk '{print $1}')
echo "  reference: $REF_HASH"
echo "  expected:  $EXPECTED"

if [ "$REF_HASH" != "$EXPECTED" ]; then
    echo "FAIL: reference implementation diverges from canonical replay" >&2
    exit 1
fi
echo "  PASS: reference matches canonical"

# Compare the production binary too, if present.
if [ -x "bin/rift_replay-linux-x86_64" ]; then
    echo ""
    echo "=== Production binary vs canonical replay ==="
    bin/rift_replay-linux-x86_64 \
        "$BRAIN" \
        "$INPUT" \
        --ticks 10000 \
        --output-binary "$TMPDIR_BIN/prod-replay.bin"
    PROD_HASH=$(sha256sum "$TMPDIR_BIN/prod-replay.bin" | awk '{print $1}')
    echo "  production: $PROD_HASH"
    if [ "$PROD_HASH" != "$EXPECTED" ]; then
        echo "FAIL: production binary diverges from canonical replay" >&2
        exit 1
    fi
    echo "  PASS: production matches canonical"
fi

echo ""
echo "PASS: reference matches golden"
