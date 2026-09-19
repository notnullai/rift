#!/usr/bin/env python3
"""compare_replay.py — Packet-count alignment comparison between offline and live tick sequences.

Reads two tick-sequence files:
  tick,packet_count_at_tick,class_0_q16,class_1_q16,...

For each offline tick, finds the live tick whose packet_count_at_tick is within
JITTER_TOL of the offline tick's. Compares score vectors byte-exactly.

Exit 0 if all ticks match. Exit 1 on mismatch or missing ticks.
"""
import sys
import csv
import argparse


def read_ticks(path):
    """Read tick CSV. Returns list of (tick, packet_count, [scores...])."""
    rows = []
    with open(path, "r") as f:
        reader = csv.reader(f)
        header = next(reader)  # skip header
        for row in reader:
            if len(row) < 3:
                continue
            tick = int(row[0])
            pkt_count = int(row[1])
            scores = [int(x) for x in row[2:]]
            rows.append((tick, pkt_count, scores))
    return rows


def compare_replay(offline_path, live_path, tolerance=2):
    """Compare offline and live tick sequences aligned by packet count."""
    offline = read_ticks(offline_path)
    live = read_ticks(live_path)

    if not offline:
        print(f"ERROR: offline file empty or unreadable: {offline_path}", file=sys.stderr)
        return 1
    if not live:
        print(f"ERROR: live file empty or unreadable: {live_path}", file=sys.stderr)
        return 1

    # Build lookup: packet_count -> (tick, scores) for live
    live_by_pkt = {}
    for tick, pkt, scores in live:
        if pkt not in live_by_pkt:
            live_by_pkt[pkt] = (tick, scores)

    matched = 0
    missing = 0
    mismatched = 0
    first_missing = None
    first_mismatch = None

    for off_tick, off_pkt, off_scores in offline:
        # Find live tick within tolerance
        best = None
        best_dist = tolerance + 1
        for d in range(-tolerance, tolerance + 1):
            candidate = off_pkt + d
            if candidate in live_by_pkt:
                dist = abs(d)
                if dist < best_dist:
                    best_dist = dist
                    best = live_by_pkt[candidate]

        if best is None:
            missing += 1
            if first_missing is None:
                first_missing = (off_tick, off_pkt, off_scores)
            continue

        live_tick, live_scores = best

        # Pad to same length
        max_len = max(len(off_scores), len(live_scores))
        off_padded = off_scores + [0] * (max_len - len(off_scores))
        live_padded = live_scores + [0] * (max_len - len(live_scores))

        if off_padded != live_padded:
            mismatched += 1
            if first_mismatch is None:
                first_mismatch = (off_tick, off_pkt, off_padded, live_tick, live_padded)
        else:
            matched += 1

    total = len(offline)
    print(f"Results: {matched}/{total} matched, {missing} missing, {mismatched} mismatched")

    if first_missing:
        t, p, s = first_missing
        print(f"First missing: offline tick={t}, packet_count={p}, scores={s[:8]}...")
    if first_mismatch:
        ot, op, os_, lt, ls = first_mismatch
        print(f"First mismatch: offline tick={ot} pkt={op}")
        print(f"  offline: {os_[:8]}...")
        print(f"  live:    {ls[:8]}...")

    if missing > 0 or mismatched > 0:
        print("FAIL")
        return 1

    print("PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Compare offline and live replay tick sequences")
    parser.add_argument("offline", help="Offline ticks CSV (from rift_replay --output-ticks)")
    parser.add_argument("live", help="Live ticks CSV (from rift_scores_reader --format ticks)")
    parser.add_argument("--tolerance", type=int, default=2,
                        help="Max packet-count difference for alignment (default: 2)")
    args = parser.parse_args()

    sys.exit(compare_replay(args.offline, args.live, args.tolerance))


if __name__ == "__main__":
    main()
