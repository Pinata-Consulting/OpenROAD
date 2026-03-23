# GateSim: JIT Gate-Level Simulator for SAIF/VCD Generation

## Status: PoC in progress

### What works

- **GateSim engine** (`src/dbSta/src/GateSim.{cc,hh}`): Cycle-based 2-state
  gate-level simulator. Reads cell functions from Liberty (`FuncExpr`),
  identifies FFs from `Sequential`, topologically sorts combinational logic,
  runs with xorshift32 random stimulus. Outputs both SAIF and VCD.
- **Tcl command**: `simulate_saif -cycles N [-saif file] [-vcd file] [-seed S]`
  registered via `dbSta.i` / `dbSta.tcl`. No OpenSTA changes needed.
- **Bazel build**: `//src/dbSta:GateSim` library, wired into `//src/dbSta:ui`.
  Full `openroad` binary builds and links.
- **Test scaffolding** (`test/orfs/gatesim/`):
  - `cells.lib` — minimal Liberty with INV, AND2, OR2, XOR2, DFF
  - `cells.lef` — matching LEF (needed by OpenROAD's `read_verilog`)
  - `cells.v` — behavioral Verilog for same cells (for Verilator)
  - `counter.v` — tiny gate-level netlist (2-bit counter with XOR feedback)
  - `simulate.cpp` — Verilator testbench with same xorshift32 PRNG + seed
  - `gatesim.tcl` — OpenROAD script to run GateSim
  - `compare_vcd.py` — VCD signal comparator (maps signal names between tools)
  - `compare_vcd_test.py` — unit test for the comparator itself (**passes**)
  - `BUILD` — Bazel rules for Verilator sim, GateSim run, VCD comparison
  - `run_test.sh`, `run_python_test.sh` — test wrappers

### What doesn't work yet

1. **Clock not found**: `simulate_saif` errors with "No clock found" on real
   designs (MockArray). The SDC clock pin lookup doesn't match the network pin
   object. Needs debugging — likely a `cmdSdc()` vs network pin identity issue.
   The tiny `counter.v` test has not been tried end-to-end yet.

2. **VCD signal naming**: GateSim VCD uses `network->pathName(pin)` for signal
   names. Need to verify these match what `compare_vcd.py` expects, and that
   the mapping in `run_test.sh` is correct for the counter design.

3. **VCD identifier overflow**: Current VCD writer uses single-char identifiers
   (`'!' + i`), which overflows at 94 signals. Fine for the unit test, needs
   multi-char IDs for real designs.

4. **Primary input ordering**: `applyStimulus` iterates `primary_input_nets_`
   which depends on `InstancePinIterator` order. The Verilator testbench must
   apply stimulus in the **exact same order** (`reset` then `d_in`). If the
   network iterator returns them in a different order, VCDs won't match.
   **Fix**: sort `primary_input_nets_` by port name so both sides agree.

5. **Power A/B test**: Not written yet. Should run both paths on the same
   design and compare `report_power` numbers:
   - Path A: Verilator → VCD → `read_vcd` → `report_power`
   - Path B: GateSim → SAIF → `read_saif` → `report_power`

### TODO

- [ ] Fix clock pin lookup (debug with counter.v first)
- [ ] Sort primary inputs by name for deterministic stimulus ordering
- [ ] Run `bazelisk test //test/orfs/gatesim:gatesim_test` end-to-end
- [ ] Add power A/B comparison test (SAIF vs VCD power numbers)
- [ ] Extend `compare_vcd_test.py` with real VCD outputs once working
- [ ] Multi-char VCD identifiers for large designs
- [ ] Write feature request `.md` with A/B numbers and `git diff` patch

### Architecture

```
Liberty (.lib)              Verilog netlist
    │                            │
    ▼                            ▼
FuncExpr trees ◄── Network API ──► Instance/Pin/Net graph
    │                                    │
    ▼                                    ▼
GateSim::buildModel()           topological sort
    │
    ▼
simulate loop:
    for each cycle:
        applyStimulus (xorshift32)
        rising edge:  clockFlipFlops → evalCombinational → updateCounters
        falling edge: evalCombinational → updateCounters
    │
    ├──► writeSaif()  →  .saif file  →  read_saif  →  report_power
    └──► writeVcd()   →  .vcd file   →  compare with Verilator
```

### Motivation

Drop Verilator as a **required** dependency for SAIF-based power analysis.
OpenROAD already has the netlist and Liberty cell functions in memory — it can
simulate directly. Verilator remains available for complex testbenches (DPI,
syscalls, CPU simulation).

### Future: JIT + bit-parallel

The interpreted PoC evaluates one FuncExpr tree per cell per cycle. Two
optimizations can make it fast enough for large designs:

1. **JIT compilation** (Xbyak for x86-64, Xbyak_aarch64 for ARM): compile
   each combinational cone into native code. ~5-20 instructions per cell.
2. **Bit-parallel simulation**: pack 64 cycles into a `uint64_t` per net.
   AND/OR/XOR become single bitwise ops. Transitions: `popcount(v ^ (v >> 1))`.
   This is the ESSENT technique (Berkeley).

### References

- Futamura 1971 — partial evaluation
- ESSENT (Berkeley) — bit-parallel gate-level simulation
- Bellard, USENIX ATC 2005 — QEMU binary translation
- Neumann, PVLDB 2011 — JIT-compiled query plans
