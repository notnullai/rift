#!/usr/bin/env python3
"""check_anomaly_pattern.py — Window-based sanity check for anomaly detection.

Reads a tick CSV (from rift_scores_reader --format ticks) and asserts:
  - Baseline window: all class scores below threshold (engine is silent)
  - Attack window: at least one class score above threshold (engine fires)
  - Recovery window: all class scores below threshold (engine returns to silent)

Exit 0 if all assertions pass. Exit 1 on failure.
"""
import sys
import csv
import argparse


def read_ticks(path):
    """Read tick CSV. Returns list of (tick, packet_count, [scores...])."""
    rows = []
    with open(path, "r") as f:
        reader = csv.reader(f)
        header = next(reader)
        for row in reader:
            if len(row) < 3:
                continue
            tick = int(row[0])
            pkt_count = int(row[1])
            scores = [int(x) for x in row[2:]]
            rows.append((tick, pkt_count, scores))
    return rows


def check_window(rows, start_tick, end_tick, expect_silent, label, threshold):
    """Check a window of ticks. Returns (pass, details)."""
    window = [r for r in rows if start_tick <= r[0] <= end_tick]
    if not window:
        return True, f"{label}: no ticks in range [{start_tick}, {end_tick}]"

    max_score = 0
    max_class = -1
    max_tick = -1
    for tick, _, scores in window:
        for c, s in enumerate(scores):
            if abs(s) > max_score:
                max_score = abs(s)
                max_class = c
                max_tick = tick

    if expect_silent:
        if max_score >= threshold:
            return False, (
                f"{label}: FAIL — max score {max_score} (class {max_class}, tick {max_tick}) "
                f">= threshold {threshold} in silent window [{start_tick}, {end_tick}]"
            )
        return True, f"{label}: OK — max score {max_score} < threshold {threshold} (silent)"
    else:
        if max_score < threshold:
            return False, (
                f"{label}: FAIL — max score {max_score} < threshold {threshold} "
                f"in active window [{start_tick}, {end_tick}]"
            )
        return True, f"{label}: OK — max score {max_score} >= threshold {threshold} (active)"


def main():
    parser = argparse.ArgumentParser(description="Check anomaly detection pattern in tick data")
    parser.add_argument("ticks_file", help="Tick CSV (from rift_scores_reader --format ticks)")
    parser.add_argument("--baseline-window", default="0,2000",
                        help="Baseline tick range (default: 0,2000)")
    parser.add_argument("--attack-window", default="2500,4500",
                        help="Attack tick range (default: 2500,4500)")
    parser.add_argument("--recovery-window", default="5000,7000",
                        help="Recovery tick range (default: 5000,7000)")
    parser.add_argument("--threshold", type=int, default=64,
                        help="Score threshold for silence/activity (default: 64)")
    parser.add_argument("--expect-baseline-silent", action="store_true", default=True,
                        help="Expect baseline window to be silent")
    parser.add_argument("--expect-attack-loud", action="store_true", default=True,
                        help="Expect attack window to show activity")
    parser.add_argument("--expect-recovery-silent", action="store_true", default=True,
                        help="Expect recovery window to be silent")
    args = parser.parse_args()

    rows = read_ticks(args.ticks_file)
    if not rows:
        print("ERROR: no tick data", file=sys.stderr)
        sys.exit(1)

    bl_start, bl_end = map(int, args.baseline_window.split(","))
    at_start, at_end = map(int, args.attack_window.split(","))
    rc_start, rc_end = map(int, args.recovery_window.split(","))

    all_pass = True
    details = []

    ok, msg = check_window(rows, bl_start, bl_end, args.expect_baseline_silent,
                           "baseline", args.threshold)
    details.append(msg)
    if not ok:
        all_pass = False

    ok, msg = check_window(rows, at_start, at_end, not args.expect_attack_loud,
                           "attack", args.threshold)
    details.append(msg)
    if not ok:
        all_pass = False

    ok, msg = check_window(rows, rc_start, rc_end, args.expect_recovery_silent,
                           "recovery", args.threshold)
    details.append(msg)
    if not ok:
        all_pass = False

    for d in details:
        print(d)

    if all_pass:
        print("PASS")
        sys.exit(0)
    else:
        print("FAIL")
        sys.exit(1)


if __name__ == "__main__":
    main()
