// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "GateSim.hh"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "sta/Clock.hh"
#include "sta/FuncExpr.hh"
#include "sta/Liberty.hh"
#include "sta/Network.hh"
#include "sta/PortDirection.hh"
#include "sta/Report.hh"
#include "sta/Sdc.hh"
#include "sta/Sequential.hh"
#include "sta/Sta.hh"

namespace sta {

GateSim::GateSim(Sta *sta) :
  StaState(sta),
  sta_(sta),
  clock_net_id_(-1)
{
}

void
GateSim::simulate(int cycles,
                  const char *saif_file,
                  const char *vcd_file,
                  uint32_t seed)
{
  buildModel();

  if (clock_net_id_ < 0) {
    report_->error(1900, "No clock found for gate-level simulation.");
    return;
  }

  report_->reportLine("GateSim: %zu nets, %zu combinational outputs, "
                      "%zu flip-flops, %zu primary inputs",
                      nets_.size(), comb_outputs_.size(),
                      flip_flops_.size(), primary_input_nets_.size());

  // Initialize all nets to 0.
  for (auto &net : nets_) {
    net.value = false;
    net.t0 = 0;
    net.t1 = 0;
    net.tc = 0;
  }

  for (auto &ff : flip_flops_) {
    ff.stored = false;
    ff.last_clock = false;
  }

  // Open VCD file if requested.
  FILE *vcd_f = nullptr;
  if (vcd_file) {
    vcd_f = fopen(vcd_file, "w");
    if (!vcd_f) {
      report_->error(1904, "Cannot open %s for writing.", vcd_file);
      return;
    }
    writeVcdHeader(vcd_f);
    prev_values_.assign(nets_.size(), false);
  }

  uint32_t rng = seed;
  uint64_t time = 0;

  // Dump initial values.
  if (vcd_f) {
    // All signals start at 0, dump all.
    fprintf(vcd_f, "#0\n");
    for (size_t i = 0; i < nets_.size(); i++)
      fprintf(vcd_f, "%c%c\n", nets_[i].value ? '1' : '0',
              static_cast<char>('!' + i));
    for (size_t i = 0; i < nets_.size(); i++)
      prev_values_[i] = nets_[i].value;
  }

  for (int cycle = 0; cycle < cycles; cycle++) {
    applyStimulus(cycle, rng);

    // Rising clock edge.
    nets_[clock_net_id_].value = true;
    clockFlipFlops();
    evalCombinational();
    updateCounters();
    time++;
    if (vcd_f)
      writeVcdValues(vcd_f, time);

    // Falling clock edge.
    nets_[clock_net_id_].value = false;
    evalCombinational();
    updateCounters();
    time++;
    if (vcd_f)
      writeVcdValues(vcd_f, time);
  }

  if (vcd_f) {
    fclose(vcd_f);
    report_->reportLine("GateSim: wrote VCD %s (%d cycles)", vcd_file, cycles);
  }

  if (saif_file) {
    writeSaif(saif_file, cycles);
    report_->reportLine("GateSim: wrote SAIF %s (%d cycles)", saif_file, cycles);
  }
}

static uint32_t
xorshift(uint32_t &state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

void
GateSim::applyStimulus(int, uint32_t &rng)
{
  for (int net_id : primary_input_nets_) {
    bool old_val = nets_[net_id].value;
    bool new_val = (xorshift(rng) & 1) != 0;
    nets_[net_id].value = new_val;
    if (old_val != new_val)
      nets_[net_id].tc++;
  }
}

int
GateSim::getOrCreateNetId(const Net *net, const Pin *pin)
{
  auto it = net_id_map_.find(net);
  if (it != net_id_map_.end())
    return it->second;
  int id = static_cast<int>(nets_.size());
  nets_.push_back({pin, net, 0, 0, 0, false});
  net_id_map_[net] = id;
  return id;
}

bool
GateSim::evalExpr(const FuncExpr *expr,
                  const std::unordered_map<const LibertyPort*, int> &inputs)
{
  switch (expr->op()) {
  case FuncExpr::Op::port: {
    auto it = inputs.find(expr->port());
    if (it != inputs.end())
      return nets_[it->second].value;
    return false;
  }
  case FuncExpr::Op::not_:
    return !evalExpr(expr->left(), inputs);
  case FuncExpr::Op::and_:
    return evalExpr(expr->left(), inputs) && evalExpr(expr->right(), inputs);
  case FuncExpr::Op::or_:
    return evalExpr(expr->left(), inputs) || evalExpr(expr->right(), inputs);
  case FuncExpr::Op::xor_:
    return evalExpr(expr->left(), inputs) != evalExpr(expr->right(), inputs);
  case FuncExpr::Op::one:
    return true;
  case FuncExpr::Op::zero:
    return false;
  }
  return false;
}

void
GateSim::evalCombinational()
{
  for (auto &comb : comb_outputs_) {
    bool new_val = evalExpr(comb.func, comb.inputs);
    bool old_val = nets_[comb.net_id].value;
    if (new_val != old_val) {
      nets_[comb.net_id].value = new_val;
      nets_[comb.net_id].tc++;
    }
  }
}

void
GateSim::clockFlipFlops()
{
  for (auto &ff : flip_flops_) {
    bool clk = nets_[ff.clock_net_id].value;
    if (clk && !ff.last_clock) {
      ff.stored = nets_[ff.data_net_id].value;
      bool old_q = nets_[ff.q_net_id].value;
      if (ff.stored != old_q) {
        nets_[ff.q_net_id].value = ff.stored;
        nets_[ff.q_net_id].tc++;
      }
      if (ff.qn_net_id >= 0) {
        bool old_qn = nets_[ff.qn_net_id].value;
        bool new_qn = !ff.stored;
        if (new_qn != old_qn) {
          nets_[ff.qn_net_id].value = new_qn;
          nets_[ff.qn_net_id].tc++;
        }
      }
    }
    ff.last_clock = clk;
  }
}

void
GateSim::updateCounters()
{
  for (auto &net : nets_) {
    if (net.value)
      net.t1++;
    else
      net.t0++;
  }
}

////////////////////////////////////////////////////////////////
// VCD output
////////////////////////////////////////////////////////////////

void
GateSim::writeVcdHeader(FILE *f)
{
  Network *network = network_;

  fprintf(f, "$timescale 1ps $end\n");
  fprintf(f, "$scope module top $end\n");

  // Assign short VCD identifiers to each net.
  // Use printable ASCII starting from '!' (0x21).
  for (size_t i = 0; i < nets_.size(); i++) {
    const char *name = network->pathName(nets_[i].pin);
    // VCD identifier: single char for small nets, multi-char for large.
    char id = static_cast<char>('!' + i);
    fprintf(f, "$var wire 1 %c %s $end\n", id, name);
  }

  fprintf(f, "$upscope $end\n");
  fprintf(f, "$enddefinitions $end\n");
}

void
GateSim::writeVcdValues(FILE *f, uint64_t time)
{
  bool any_changed = false;
  for (size_t i = 0; i < nets_.size(); i++) {
    if (nets_[i].value != prev_values_[i]) {
      any_changed = true;
      break;
    }
  }
  if (!any_changed)
    return;

  fprintf(f, "#%" PRIu64 "\n", time);
  for (size_t i = 0; i < nets_.size(); i++) {
    if (nets_[i].value != prev_values_[i]) {
      fprintf(f, "%c%c\n", nets_[i].value ? '1' : '0',
              static_cast<char>('!' + i));
      prev_values_[i] = nets_[i].value;
    }
  }
}

////////////////////////////////////////////////////////////////
// Model building
////////////////////////////////////////////////////////////////

void
GateSim::buildModel()
{
  Network *network = network_;
  Sdc *sdc = sta_->cmdSdc();

  nets_.clear();
  net_id_map_.clear();
  comb_outputs_.clear();
  flip_flops_.clear();
  primary_input_nets_.clear();
  clock_net_id_ = -1;

  // Find clock from SDC.
  const Pin *clock_pin = nullptr;
  const ClockSeq &clocks = sdc->clocks();
  if (!clocks.empty()) {
    const PinSet &pins = clocks[0]->pins();
    if (!pins.empty())
      clock_pin = *pins.begin();
  }

  // Create nets for top-level ports.
  Instance *top = network->topInstance();
  InstancePinIterator *top_pin_iter = network->pinIterator(top);
  while (top_pin_iter->hasNext()) {
    const Pin *pin = top_pin_iter->next();
    Net *net = network->net(pin);
    if (!net)
      continue;
    if (network->isPower(net) || network->isGround(net))
      continue;
    int net_id = getOrCreateNetId(net, pin);
    PortDirection *dir = network->direction(pin);
    if (dir->isInput()) {
      if (clock_pin && pin == clock_pin)
        clock_net_id_ = net_id;
      else
        primary_input_nets_.push_back(net_id);
    }
  }
  delete top_pin_iter;

  // Track FF output nets for topological sort.
  std::set<int> ff_output_nets;

  // First pass: identify flip-flops.
  LeafInstanceIterator *inst_iter = network->leafInstanceIterator();
  while (inst_iter->hasNext()) {
    Instance *inst = inst_iter->next();
    LibertyCell *lib_cell = network->libertyCell(inst);
    if (!lib_cell || !lib_cell->hasSequentials())
      continue;

    std::unordered_map<const LibertyPort*, int> port_net;
    InstancePinIterator *pin_iter = network->pinIterator(inst);
    while (pin_iter->hasNext()) {
      const Pin *pin = pin_iter->next();
      LibertyPort *port = network->libertyPort(pin);
      Net *net = network->net(pin);
      if (port && net && !network->isPower(net) && !network->isGround(net))
        port_net[port] = getOrCreateNetId(net, pin);
    }
    delete pin_iter;

    for (const Sequential &seq : lib_cell->sequentials()) {
      if (!seq.isRegister())
        continue;

      FlipFlop ff;
      ff.last_clock = false;
      ff.stored = false;

      const FuncExpr *clk_expr = seq.clock();
      if (clk_expr && clk_expr->op() == FuncExpr::Op::port) {
        auto it = port_net.find(clk_expr->port());
        ff.clock_net_id = (it != port_net.end()) ? it->second : clock_net_id_;
      } else {
        ff.clock_net_id = clock_net_id_;
      }

      const FuncExpr *data_expr = seq.data();
      ff.data_net_id = -1;
      if (data_expr && data_expr->op() == FuncExpr::Op::port) {
        auto it = port_net.find(data_expr->port());
        if (it != port_net.end())
          ff.data_net_id = it->second;
      }
      if (ff.data_net_id < 0) {
        for (auto &[port, net_id] : port_net) {
          if (port->direction()->isInput() && !port->isPwrGnd()) {
            const char *name = port->name();
            if (name[0] == 'D' || name[0] == 'd') {
              ff.data_net_id = net_id;
              break;
            }
          }
        }
      }

      LibertyPort *q_port = seq.output();
      ff.q_net_id = -1;
      if (q_port) {
        auto it = port_net.find(q_port);
        if (it != port_net.end()) {
          ff.q_net_id = it->second;
          ff_output_nets.insert(it->second);
        }
      }

      LibertyPort *qn_port = seq.outputInv();
      ff.qn_net_id = -1;
      if (qn_port) {
        auto it = port_net.find(qn_port);
        if (it != port_net.end()) {
          ff.qn_net_id = it->second;
          ff_output_nets.insert(it->second);
        }
      }

      if (ff.data_net_id >= 0 && ff.q_net_id >= 0)
        flip_flops_.push_back(ff);
    }
  }
  delete inst_iter;

  // Second pass: combinational outputs.
  std::vector<CombOutput> unsorted_combs;
  inst_iter = network->leafInstanceIterator();
  while (inst_iter->hasNext()) {
    Instance *inst = inst_iter->next();
    LibertyCell *lib_cell = network->libertyCell(inst);
    if (!lib_cell)
      continue;

    std::unordered_map<const LibertyPort*, int> port_net;
    InstancePinIterator *pin_iter = network->pinIterator(inst);
    while (pin_iter->hasNext()) {
      const Pin *pin = pin_iter->next();
      LibertyPort *port = network->libertyPort(pin);
      Net *net = network->net(pin);
      if (port && net && !network->isPower(net) && !network->isGround(net))
        port_net[port] = getOrCreateNetId(net, pin);
    }
    delete pin_iter;

    Cell *cell = network->cell(inst);
    CellPortBitIterator *port_iter = network->portBitIterator(cell);
    while (port_iter->hasNext()) {
      Port *port = port_iter->next();
      LibertyPort *lib_port = network->libertyPort(port);
      if (!lib_port || !lib_port->direction()->isOutput())
        continue;
      FuncExpr *func = lib_port->function();
      if (!func)
        continue;

      auto out_it = port_net.find(lib_port);
      if (out_it == port_net.end())
        continue;
      int out_net_id = out_it->second;

      if (ff_output_nets.count(out_net_id))
        continue;

      CombOutput comb;
      comb.net_id = out_net_id;
      comb.func = func;
      comb.inputs = port_net;
      unsorted_combs.push_back(std::move(comb));
    }
    delete port_iter;
  }
  delete inst_iter;

  // Topological sort by combinational depth.
  std::unordered_map<int, int> net_level;
  for (int pi : primary_input_nets_)
    net_level[pi] = 0;
  if (clock_net_id_ >= 0)
    net_level[clock_net_id_] = 0;
  for (int ff_net : ff_output_nets)
    net_level[ff_net] = 0;

  bool progress = true;
  while (progress) {
    progress = false;
    for (auto &comb : unsorted_combs) {
      if (net_level.count(comb.net_id))
        continue;
      int max_input_level = -1;
      bool all_resolved = true;
      for (auto &[port, net_id] : comb.inputs) {
        if (port->direction()->isOutput())
          continue;
        auto it = net_level.find(net_id);
        if (it != net_level.end()) {
          max_input_level = std::max(max_input_level, it->second);
        } else {
          all_resolved = false;
          break;
        }
      }
      if (all_resolved) {
        net_level[comb.net_id] = max_input_level + 1;
        progress = true;
      }
    }
  }

  std::sort(unsorted_combs.begin(), unsorted_combs.end(),
            [&net_level](const CombOutput &a, const CombOutput &b) {
              int la = net_level.count(a.net_id) ? net_level[a.net_id] : 999999;
              int lb = net_level.count(b.net_id) ? net_level[b.net_id] : 999999;
              return la < lb;
            });
  comb_outputs_ = std::move(unsorted_combs);
}

////////////////////////////////////////////////////////////////
// SAIF output
////////////////////////////////////////////////////////////////

void
GateSim::writeSaif(const char *filename, int cycles)
{
  FILE *f = fopen(filename, "w");
  if (!f) {
    report_->error(1901, "Cannot open %s for writing.", filename);
    return;
  }

  Network *network = network_;
  uint64_t duration = static_cast<uint64_t>(cycles) * 2;

  fprintf(f, "(SAIFILE\n");
  fprintf(f, "(SAIFVERSION \"2.0\")\n");
  fprintf(f, "(DIRECTION \"backward\")\n");
  fprintf(f, "(TIMESCALE 1 ps)\n");
  fprintf(f, "(DURATION %" PRIu64 ")\n", duration);

  std::map<std::string, std::vector<std::pair<std::string, int>>> inst_pins;

  LeafInstanceIterator *inst_iter = network->leafInstanceIterator();
  while (inst_iter->hasNext()) {
    Instance *inst = inst_iter->next();
    const char *inst_name = network->name(inst);
    InstancePinIterator *pin_iter = network->pinIterator(inst);
    while (pin_iter->hasNext()) {
      const Pin *pin = pin_iter->next();
      LibertyPort *port = network->libertyPort(pin);
      Net *net = network->net(pin);
      if (!port || !net)
        continue;
      if (network->isPower(net) || network->isGround(net))
        continue;
      if (port->isPwrGnd())
        continue;
      auto it = net_id_map_.find(net);
      if (it == net_id_map_.end())
        continue;
      inst_pins[inst_name].emplace_back(port->name(), it->second);
    }
    delete pin_iter;
  }
  delete inst_iter;

  fprintf(f, "(INSTANCE gate_sim\n");
  for (auto &[inst_name, pins] : inst_pins) {
    fprintf(f, "  (INSTANCE %s\n", inst_name.c_str());
    fprintf(f, "    (NET\n");
    for (auto &[port_name, net_id] : pins) {
      const SimNet &sn = nets_[net_id];
      fprintf(f, "      (%s\n", port_name.c_str());
      fprintf(f, "        (T0 %" PRIu64 ") (T1 %" PRIu64 ") (TX 0)\n",
              sn.t0, sn.t1);
      fprintf(f, "        (TC %" PRIu64 ") (IG 0)\n", sn.tc);
      fprintf(f, "      )\n");
    }
    fprintf(f, "    )\n");
    fprintf(f, "  )\n");
  }
  fprintf(f, ")\n");
  fprintf(f, ")\n");
  fclose(f);
}

} // namespace
