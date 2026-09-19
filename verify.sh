#!/usr/bin/env bash
set -euo pipefail

BRAIN=/rift/data/reflex-brain-rift-demo.rift
INPUT=/rift/data/test_input.bin
EXPECTED_HASH=$(awk '{print $1}' /rift/data/reflex-brain-rift-demo-replay.sha256)

if [ "${1:-}" = "--verify" ]; then
    echo "Verifying reference replay..."
    echo "  Brain SHA-256:   $(sha256sum "$BRAIN" | awk '{print $1}')"

    rift_replay "$BRAIN" "$INPUT" --ticks 10000 \
        --output-binary /tmp/replay.bin
    ACTUAL_HASH=$(sha256sum /tmp/replay.bin | awk '{print $1}')

    echo "  Replay SHA-256:  $ACTUAL_HASH"
    echo "  Expected:        $EXPECTED_HASH"

    if [ "$ACTUAL_HASH" != "$EXPECTED_HASH" ]; then
        echo "FAIL: replay hash mismatch" >&2
        exit 1
    fi

    echo "PASS: reference replay matches."
    exit 0
fi

exec rift_replay "$@"
