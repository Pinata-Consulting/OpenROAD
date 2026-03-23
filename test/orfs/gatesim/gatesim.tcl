read_liberty $::env(LIB_FILE)
read_lef $::env(LEF_FILE)
read_verilog $::env(NETLIST)
link_design counter

create_clock -period 10 [get_ports clock]

simulate_saif -cycles $::env(CYCLES) -vcd $::env(VCD_OUTPUT) -seed 42

exit
