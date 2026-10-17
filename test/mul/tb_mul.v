`default_nettype none
`timescale 1ns / 1ps

// Spec 2 exhaustive test for modq_mul: every a, b in 0..3328, a start every
// SPACING clocks (1 = every clock), p_out == 2197 * a * b mod 3329 exactly LAT
// clocks after each start, no other p_valid. `B_STEP > 1 samples b for a quick run.

`ifndef LAT
`define LAT 3
`endif
`ifndef SPACING
`define SPACING 1
`endif
`ifndef B_STEP
`define B_STEP 1
`endif

module tb_mul;
  reg         clk = 1'b0, rst_n = 1'b0, start = 1'b0, flush = 1'b0;
  reg  [11:0] a = 12'd0, b = 12'd0;
  wire [11:0] p;
  wire        pv, rdy;

  modq_mul dut (.clk(clk), .rst_n(rst_n), .start(start), .a_in(a), .b_in(b),
                .flush(flush), .p_out(p), .p_valid(pv), .ready(rdy));

  always #1 clk = ~clk;

  // scoreboard: operands of each start, delayed by LAT clocks
  reg [11:0] sa [0:`LAT-1];
  reg [11:0] sb [0:`LAT-1];
  reg        sv [0:`LAT-1];
  integer i, ia, ib, errs = 0, n = 0;
  reg [31:0] exp;

  always @(posedge clk) begin
    if (rst_n) begin
      if (pv !== sv[`LAT-1]) begin
        if (errs < 5) $display("p_valid=%b at the wrong time", pv);
        errs = errs + 1;
      end else if (pv) begin
        exp = (2197 * sa[`LAT-1] % 3329) * sb[`LAT-1] % 3329;
        if (p !== exp[11:0]) begin
          if (errs < 5) $display("a=%0d b=%0d: got %0d, expected %0d", sa[`LAT-1], sb[`LAT-1], p, exp);
          errs = errs + 1;
        end
        n = n + 1;
      end
    end
    for (i = `LAT-1; i > 0; i = i - 1) begin
      sa[i] <= sa[i-1]; sb[i] <= sb[i-1]; sv[i] <= sv[i-1];
    end
    sa[0] <= a; sb[0] <= b; sv[0] <= start;
  end

  initial begin
    for (i = 0; i < `LAT; i = i + 1) sv[i] = 1'b0;
    repeat (3) @(posedge clk);
    @(negedge clk) rst_n = 1'b1;
    for (ia = 0; ia < 3329; ia = ia + 1)
      for (ib = 0; ib < 3329; ib = ib + `B_STEP) begin
        @(negedge clk); start = 1'b1; a = ia; b = ib;
        repeat (`SPACING - 1) begin @(negedge clk); start = 1'b0; end
      end
    @(negedge clk); start = 1'b0;
    repeat (`LAT + 2) @(negedge clk);
    $display("modq_mul: %0d products checked, %0d errors", n, errs);
    if (errs != 0 || n == 0) $fatal(1, "FAILED");
    $finish;
  end
endmodule
