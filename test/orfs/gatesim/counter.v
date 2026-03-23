// 2-bit counter with XOR feedback — gate-level netlist using cells.lib cells.
// Inputs: clock, reset, d_in
// Outputs: q0, q1

module counter(input clock, input reset, input d_in,
               output q0, output q1);

  wire xor_out, and_out, or_out, inv_reset;
  wire q0_int, q1_int, q0n_int, q1n_int;

  // d_in XOR q0 -> first FF input
  XOR2 u_xor(.A(d_in), .B(q0_int), .Y(xor_out));

  // reset inverted
  INV u_inv_rst(.A(reset), .Y(inv_reset));

  // Gate the XOR output with !reset
  AND2 u_and_d0(.A(xor_out), .B(inv_reset), .Y(and_out));

  // First FF
  DFF u_ff0(.CLK(clock), .D(and_out), .Q(q0_int), .QN(q0n_int));

  // q0 AND d_in -> second FF input
  AND2 u_and_d1(.A(q0_int), .B(d_in), .Y(or_out));

  // Second FF
  DFF u_ff1(.CLK(clock), .D(or_out), .Q(q1_int), .QN(q1n_int));

  assign q0 = q0_int;
  assign q1 = q1_int;

endmodule
