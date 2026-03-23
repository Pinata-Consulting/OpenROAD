#!/bin/bash
# Compare Verilator and GateSim VCD outputs.
# Args: compare_vcd.py verilator.vcd gatesim.vcd
set -e

SCRIPT="$1"
VERILATOR_VCD="$2"
GATESIM_VCD="$3"

# Map: Verilator hierarchical names -> GateSim flat names.
# Verilator names are TOP.counter.signal, GateSim uses pin path names.
# The exact mapping depends on how each tool names signals — we compare
# the output port values which both tools must agree on.
MAPPING="TOP.counter.q0=q0,TOP.counter.q1=q1"

exec python3 "$SCRIPT" "$VERILATOR_VCD" "$GATESIM_VCD" "$MAPPING"
