#!/usr/bin/env python3
"""check_tick_budget.py — Histogram-based tick budget assertion.

Reads a tick histogram CSV (from rift_tick_timer --format csv) and asserts
that the percentage of ticks exceeding the budget is below a threshold.

CSV format: histogram,bucket_lo_ns,count
Bucket i covers [2^i, 2^(i+1)) nanoseconds.

Exit 0 if budget honored. Exit 1 on overrun.
"""
import sys
import csv
import argparse


def read_histogram(path, histogram_name="tick_time"):
    """Read histogram CSV. Returns dict of bucket_lo_ns -> count."""
    hist = {}
    with open(path, "r") as f:
        reader = csv.reader(f)
        header = next(reader)  # skip header
        for row in reader:
            if len(row) < 3:
                continue
            name, bucket_lo, count = row[0], int(row[1]), int(row[2])
            if name == histogram_name:
                hist[bucket_lo] = count
    return hist


def compute_overruns(hist, budget_ns):
    """Count ticks exceeding budget. bucket_lo >= budget means entire bucket overruns."""
    overruns = 0
    total = 0
    for bucket_lo, count in hist.items():
        total += count
        if bucket_lo >= budget_ns:
            overruns += count
    return overruns, total


def main():
    parser = argparse.ArgumentParser(description="Check tick-time budget from histogram")
    parser.add_argument("histogram_csv", help="Histogram CSV (from rift_tick_timer --format csv)")
    parser.add_argument("--budget-us", type=int, default=10,
                        help="Tick budget in microseconds (default: 10)")
    parser.add_argument("--max-overrun-pct", type=float, default=0.1,
                        help="Max allowed overrun percentage (default: 0.1)")
    parser.add_argument("--histogram-name", default="tick_time",
                        help="Histogram name to check (default: tick_time)")
    args = parser.parse_args()

    budget_ns = args.budget_us * 1000
    hist = read_histogram(args.histogram_csv, args.histogram_name)

    if not hist:
        print(f"ERROR: no data for histogram '{args.histogram_name}' in {args.histogram_csv}",
              file=sys.stderr)
        sys.exit(1)

    overruns, total = compute_overruns(hist, budget_ns)
    overrun_pct = 100.0 * overruns / total if total > 0 else 0.0

    print(f"Budget: {args.budget_us} us ({budget_ns} ns)")
    print(f"Total ticks: {total}")
    print(f"Overruns: {overruns} ({overrun_pct:.4f}%)")
    print(f"Max allowed: {args.max_overrun_pct:.4f}%")

    if overrun_pct > args.max_overrun_pct:
        print(f"FAIL: overrun {overrun_pct:.4f}% > {args.max_overrun_pct:.4f}%")
        sys.exit(1)

    print("PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
