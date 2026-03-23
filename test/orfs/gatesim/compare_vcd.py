#!/usr/bin/env python3
"""Compare two VCD files for identical signal values at each timestep.

Usage: compare_vcd.py <verilator.vcd> <gatesim.vcd> <signal_mapping>

signal_mapping is a comma-separated list of verilator_name=gatesim_name pairs.
Example: "TOP.counter.q0=q0,TOP.counter.q1=q1"

Only mapped signals are compared. Exits 0 if identical, 1 if different.
"""

import re
import sys


def parse_vcd(filename):
    """Parse VCD into {signal_name: [(time, value), ...]}."""
    signals = {}  # id -> name
    values = {}   # name -> [(time, value)]
    current_time = 0

    with open(filename) as f:
        in_defs = True
        for line in f:
            line = line.strip()
            if not line:
                continue

            if in_defs:
                if line == "$enddefinitions $end":
                    in_defs = False
                    continue
                m = re.match(r'\$var\s+\w+\s+\d+\s+(\S+)\s+(\S+)\s+\$end', line)
                if m:
                    var_id, var_name = m.group(1), m.group(2)
                    signals[var_id] = var_name
                    values[var_name] = []
                continue

            if line.startswith('#'):
                current_time = int(line[1:])
                continue

            # Single-bit value change: 0x or 1x where x is the identifier
            if len(line) >= 2 and line[0] in '01xXzZ':
                val = line[0]
                var_id = line[1:]
                if var_id in signals:
                    name = signals[var_id]
                    values[name].append((current_time, val))

    return values


def get_value_at_time(changes, time):
    """Get the signal value at a given time from a list of (time, value) pairs."""
    val = '0'
    for t, v in changes:
        if t > time:
            break
        val = v
    return val


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <verilator.vcd> <gatesim.vcd> <mapping>")
        sys.exit(2)

    verilator_vcd = sys.argv[1]
    gatesim_vcd = sys.argv[2]
    mapping_str = sys.argv[3]

    # Parse mapping: verilator_name=gatesim_name,...
    mappings = []
    for pair in mapping_str.split(","):
        v_name, g_name = pair.strip().split("=")
        mappings.append((v_name.strip(), g_name.strip()))

    v_data = parse_vcd(verilator_vcd)
    g_data = parse_vcd(gatesim_vcd)

    print(f"Verilator signals: {sorted(v_data.keys())}")
    print(f"GateSim signals: {sorted(g_data.keys())}")

    # Collect all timesteps from both VCDs.
    all_times = set()
    for changes in v_data.values():
        for t, _ in changes:
            all_times.add(t)
    for changes in g_data.values():
        for t, _ in changes:
            all_times.add(t)
    all_times = sorted(all_times)

    errors = 0
    for v_name, g_name in mappings:
        if v_name not in v_data:
            print(f"WARNING: Verilator signal '{v_name}' not found")
            continue
        if g_name not in g_data:
            print(f"WARNING: GateSim signal '{g_name}' not found")
            continue

        v_changes = v_data[v_name]
        g_changes = g_data[g_name]

        for t in all_times:
            v_val = get_value_at_time(v_changes, t)
            g_val = get_value_at_time(g_changes, t)
            if v_val != g_val:
                print(f"MISMATCH at t={t}: {v_name}={v_val} vs {g_name}={g_val}")
                errors += 1
                if errors >= 20:
                    print("... (too many errors, stopping)")
                    sys.exit(1)

    if errors == 0:
        print(f"OK: {len(mappings)} signals match across {len(all_times)} timesteps")
    else:
        print(f"FAIL: {errors} mismatches")

    sys.exit(0 if errors == 0 else 1)


if __name__ == "__main__":
    main()
