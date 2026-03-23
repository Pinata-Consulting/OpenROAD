// Behavioral Verilog models matching cells.lib — for Verilator simulation.

module INV(input A, output Y);
  assign Y = ~A;
endmodule

module AND2(input A, input B, output Y);
  assign Y = A & B;
endmodule

module OR2(input A, input B, output Y);
  assign Y = A | B;
endmodule

module XOR2(input A, input B, output Y);
  assign Y = A ^ B;
endmodule

module DFF(input CLK, input D, output reg Q, output QN);
  initial Q = 0;
  always @(posedge CLK)
    Q <= D;
  assign QN = ~Q;
endmodule
