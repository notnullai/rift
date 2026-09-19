#!/bin/bash
# test_live_pipeline.sh — BPF pipeline integration test (public repo)
#
# Attaches the production XDP program to the loopback interface and asserts:
#   1. Verifier acceptance + program attachment
#   2. XDP_PASS: traffic flows through the interface untouched
#   3. Tick budget: engine ticks in time
#   4. Score readability: scores and heartbeat are observable
#
# Requires: Linux kernel 5.10+, root, python3 (stdlib only), bin/ binaries.
# Exit codes: 0 = pass, 1 = fail
set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIN_DIR="/sys/fs/bpf/rift"
BRAIN="${1:-$REPO_ROOT/data/reflex-brain-rift-demo.rift}"
BIN="$REPO_ROOT/bin"
LOADER="$BIN/rift_loader-linux-x86_64"
BPF_OBJ="$BIN/rift_xdp.bpf.o"
TICK_TIMER="$BIN/rift_tick_timer-linux-x86_64"
SCORES_READER="$BIN/rift_scores_reader-linux-x86_64"
TICK_RATE=1000
IFACE="lo"

EXIT_CODE=0
LOADED=0

pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1" >&2; EXIT_CODE=1; }

cleanup() {
    echo ""
    echo "=== Cleanup ==="
    pkill -f "rift_loader" 2>/dev/null || true
    pkill -f "rift_udp_blast" 2>/dev/null || true
    sleep 1
    ip link set dev "$IFACE" xdpgeneric off 2>/dev/null || true
    rm -rf "$PIN_DIR" 2>/dev/null || true
    echo "  Cleanup done."
}
trap cleanup EXIT

echo "=== RIFT Live Pipeline Test ==="
echo ""

# ---- Preconditions ----
echo "=== Precondition checks ==="

if [ "$(id -u)" -ne 0 ]; then
    echo "SKIP: must be root"; exit 0
fi
pass "root"

for tool in "$LOADER" "$BPF_OBJ" "$TICK_TIMER" "$SCORES_READER"; do
    [ -f "$tool" ] || fail "missing: $tool"
done
[ "$EXIT_CODE" -eq 0 ] && pass "binaries exist"

[ -f "$BRAIN" ] || fail "brain not found: $BRAIN"
[ "$EXIT_CODE" -eq 0 ] && pass "brain file exists"

echo ""

# ---- Step 1: Attach ----
echo "=== Step 1: Verifier acceptance + attach ==="

ip link set dev "$IFACE" xdpgeneric off 2>/dev/null || true
rm -rf "$PIN_DIR" 2>/dev/null || true
sleep 1

setsid "$LOADER" \
    --bpf "$BPF_OBJ" \
    --brain "$BRAIN" \
    --iface "$IFACE" \
    --tick-rate "$TICK_RATE" \
    --pin-dir "$PIN_DIR" \
    < /dev/null > /tmp/rift_pipeline_loader.log 2>&1 &
LOADER_PID=$!
sleep 5

if ! kill -0 "$LOADER_PID" 2>/dev/null && ! pgrep -f rift_loader > /dev/null 2>&1; then
    fail "loader died (verifier rejection or load failure)"
    cat /tmp/rift_pipeline_loader.log >&2
    exit 1
fi
LOADED=1
pass "loader running"

PROG_ID=$(ip link show "$IFACE" 2>/dev/null | grep -oP 'id \K[0-9]+' || echo "")
if [ -z "$PROG_ID" ]; then
    fail "no prog id on $IFACE"
    exit 1
fi
pass "program attached (id $PROG_ID)"
echo ""

# ---- Step 2: XDP_PASS ----
echo "=== Step 2: Packets pass (XDP_PASS) ==="

PING_OK=0
for i in 1 2 3 4 5; do
    if ping -c 1 -W 1 127.0.0.1 > /dev/null 2>&1; then
        PING_OK=1
        break
    fi
done
if [ "$PING_OK" = "1" ]; then
    pass "traffic flows through $IFACE with the program attached"
else
    fail "ping failed with program attached"
fi

# A burst of UDP, to give the engine something to count.
python3 - "$IFACE" <<'PYEOF' &
import socket, sys, time
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.2)
end = time.time() + 6
payload = b"x" * 1400
n = 0
while time.time() < end:
    sock.sendto(payload, ("127.0.0.1", 9))
    n += 1
    if n % 50 == 0:
        time.sleep(0.001)
PYEOF
BLAST_PID=$!
sleep 7
kill "$BLAST_PID" 2>/dev/null || true
pass "traffic generated"
echo ""

# ---- Step 3: Tick budget ----
echo "=== Step 3: Tick budget ==="

TICK_OUTPUT=$("$TICK_TIMER" --pin-dir "$PIN_DIR" 2>&1 || echo "ERROR")
if echo "$TICK_OUTPUT" | grep -q "ERROR"; then
    fail "tick timer read failed"
    echo "$TICK_OUTPUT" >&2
else
    P50=$(echo "$TICK_OUTPUT" | grep 'p50' | head -1 | sed 's/.*: *//' | tr -d '[:space:]')
    P99=$(echo "$TICK_OUTPUT" | grep 'p99   :' | head -1 | sed 's/.*: *//' | tr -d '[:space:]')
    OVERRUNS=$(echo "$TICK_OUTPUT" | grep 'overruns' | sed 's/.*: *//' | cut -d'(' -f1 | tr -d '[:space:]')

    if [ "${P50:-0}" -gt 0 ] 2>/dev/null; then
        pass "ticks observed: p50=${P50}ns p99=${P99}ns overruns=${OVERRUNS}"
        # Tick rate is 1000 Hz (1ms); p99 must stay under the 1ms budget.
        if [ "${P99:-0}" -gt 1000000 ] 2>/dev/null; then
            fail "tick p99 ${P99}ns > 1ms budget"
        fi
    else
        fail "no ticks observed: p50=${P50}"
    fi
fi
echo ""

# ---- Step 4: Score readability + heartbeat ----
echo "=== Step 4: Score readability + heartbeat ==="

SCORES=$("$SCORES_READER" --pin-dir "$PIN_DIR" --once 2>/dev/null || echo "ERROR")
if echo "$SCORES" | grep -q "ERROR"; then
    fail "score read failed"
else
    TICK_COUNT=$(echo "$SCORES" | cut -d',' -f1)
    NUM_FIELDS=$(echo "$SCORES" | tr ',' '\n' | wc -l)
    LAST_FIELD=$(echo "$SCORES" | rev | cut -d',' -f1 | rev)

    if [ "${TICK_COUNT:-0}" -gt 0 ] 2>/dev/null && [ "$NUM_FIELDS" -ge 10 ] && echo "$LAST_FIELD" | grep -qE 'LIVE|IDLE|STALLED'; then
        pass "scores readable: ticks=$TICK_COUNT fields=$NUM_FIELDS state=$LAST_FIELD"
    else
        fail "score check: ticks=$TICK_COUNT fields=$NUM_FIELDS state=$LAST_FIELD"
    fi
fi
echo ""

# ---- Summary ----
echo "=== Summary ==="
if [ "$EXIT_CODE" -eq 0 ]; then
    echo "PASS: live pipeline"
else
    echo "FAIL: pipeline test failures detected"
fi
exit $EXIT_CODE
