# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("@rules_verilator//verilator:defs.bzl", "verilator_cc_library")
load("@rules_verilator//verilog:defs.bzl", "verilog_library")

package(default_visibility = ["//:__subpackages__"])

# Verilog sources for Verilator: gate-level netlist + cell behavioral models
verilog_library(
    name = "counter_verilog",
    srcs = [
        "cells.v",
        "counter.v",
    ],
)

verilator_cc_library(
    name = "counter_verilator",
    module = ":counter_verilog",
    module_top = "counter",
    trace = True,
    vopts = [
        "--timescale 1ps/1ps",
        "-Wno-DECLFILENAME",
    ],
)

cc_binary(
    name = "verilator_sim",
    srcs = ["simulate.cpp"],
    cxxopts = ["-std=c++23"],
    deps = [":counter_verilator"],
)

# Generate Verilator VCD
genrule(
    name = "verilator_vcd",
    outs = ["verilator.vcd"],
    cmd = "$(execpath :verilator_sim) $@ 20",
    tools = [":verilator_sim"],
)

# Generate GateSim VCD via OpenROAD
genrule(
    name = "gatesim_vcd",
    srcs = [
        "cells.lib",
        "cells.lef",
        "counter.v",
        "gatesim.tcl",
    ],
    outs = ["gatesim.vcd"],
    cmd = """
        LIB_FILE=$(location cells.lib) \
        LEF_FILE=$(location cells.lef) \
        NETLIST=$(location counter.v) \
        CYCLES=20 \
        VCD_OUTPUT=$@ \
        $(execpath //:openroad) -no_init $(location gatesim.tcl)
    """,
    tools = ["//:openroad"],
)

# Compare VCDs
sh_test(
    name = "gatesim_test",
    srcs = ["run_test.sh"],
    args = [
        "$(location compare_vcd.py)",
        "$(location :verilator_vcd)",
        "$(location :gatesim_vcd)",
    ],
    data = [
        "compare_vcd.py",
        ":gatesim_vcd",
        ":verilator_vcd",
    ],
)

# Unit test for the VCD comparator itself
sh_test(
    name = "compare_vcd_test",
    srcs = ["run_python_test.sh"],
    args = ["$(location compare_vcd_test.py)"],
    data = [
        "compare_vcd.py",
        "compare_vcd_test.py",
    ],
)
