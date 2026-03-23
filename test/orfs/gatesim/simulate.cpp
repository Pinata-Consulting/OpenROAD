// Verilator testbench for counter.v
// Uses the same xorshift32 PRNG and stimulus pattern as GateSim
// so VCD outputs should match exactly.

#include <cstdint>
#include <cstdlib>
#include <print>

#include "Vcounter.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

static uint32_t xorshift(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

int main(int argc, char** argv)
{
  Verilated::commandArgs(argc, argv);

  if (argc < 3) {
    std::print(stderr, "Usage: {} <vcd_file> <cycles>\n", argv[0]);
    return 1;
  }

  const char* vcd_file = argv[1];
  int cycles = std::atoi(argv[2]);

  auto* top = new Vcounter;
  Verilated::traceEverOn(true);
  auto* vcd = new VerilatedVcdC;
  top->trace(vcd, 99);
  vcd->open(vcd_file);

  // Initialize.
  top->clock = 0;
  top->reset = 0;
  top->d_in = 0;
  top->eval();
  vcd->dump(0);

  uint32_t rng = 42;  // Same seed as GateSim default.
  uint64_t time = 0;

  for (int cycle = 0; cycle < cycles; cycle++) {
    // Apply stimulus: same order as GateSim primary_input_nets_.
    // GateSim iterates top-level input pins in network order.
    // For this test, primary inputs (excluding clock) are: reset, d_in.
    // Each gets one xorshift call, same as GateSim::applyStimulus.
    top->reset = xorshift(rng) & 1;
    top->d_in = xorshift(rng) & 1;

    // Rising clock edge.
    top->clock = 1;
    top->eval();
    time++;
    vcd->dump(time);

    // Falling clock edge.
    top->clock = 0;
    top->eval();
    time++;
    vcd->dump(time);
  }

  vcd->flush();
  vcd->close();
  top->final();
  delete top;

  return 0;
}
