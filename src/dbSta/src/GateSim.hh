// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "sta/LibertyClass.hh"
#include "sta/NetworkClass.hh"
#include "sta/StaState.hh"

namespace sta {

class FuncExpr;
class Sta;

// Cycle-based 2-state gate-level simulator for SAIF/VCD generation.
// Evaluates combinational logic from Liberty FuncExpr, handles FFs,
// and counts switching activity per net.  Single clock domain.
class GateSim : public StaState
{
public:
  GateSim(Sta *sta);

  // Run simulation and write output files.
  // saif_file and vcd_file may be nullptr to skip that output.
  void simulate(int cycles,
                const char *saif_file,
                const char *vcd_file,
                uint32_t seed);

private:
  struct SimNet {
    const Pin *pin;
    const Net *net;
    uint64_t t0;
    uint64_t t1;
    uint64_t tc;
    bool value;
  };

  struct CombOutput {
    int net_id;
    const FuncExpr *func;
    std::unordered_map<const LibertyPort*, int> inputs;
  };

  struct FlipFlop {
    int clock_net_id;
    int data_net_id;
    int q_net_id;
    int qn_net_id;
    bool last_clock;
    bool stored;
  };

  void buildModel();
  int getOrCreateNetId(const Net *net, const Pin *pin);
  bool evalExpr(const FuncExpr *expr,
                const std::unordered_map<const LibertyPort*, int> &inputs);
  void evalCombinational();
  void clockFlipFlops();
  void updateCounters();
  void applyStimulus(int cycle, uint32_t &rng);
  void writeSaif(const char *filename, int cycles);

  // VCD support.
  void writeVcdHeader(FILE *f);
  void writeVcdValues(FILE *f, uint64_t time);

  Sta *sta_;
  std::vector<SimNet> nets_;
  std::unordered_map<const Net*, int> net_id_map_;
  std::vector<CombOutput> comb_outputs_;
  std::vector<FlipFlop> flip_flops_;
  std::vector<int> primary_input_nets_;
  int clock_net_id_;

  // For VCD: map net_id to short identifier and previous value.
  std::vector<bool> prev_values_;
};

} // namespace
