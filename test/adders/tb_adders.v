`default_nettype none
`timescale 1ns / 1ps

// Spec 4 tests for modq_addsub and modq_acc (iverilog, no cocotb).
//   modq_addsub: every x in 0..3328 against every `Y_STEP-th y (1 = all
//     3329 x 3329 pairs), REG_OUT = 0 and REG_OUT = 1 side by side, plus the
//     spec's vector table and the edge values 0 and 3328.
//   modq_acc: the spec's vectors, ld priority, hold, reset, and `ACC_OPS
//     random operations against a software model.
// Ends with $fatal on any error, so make fails.

`ifndef Y_STEP
`define Y_STEP 1
`endif
`ifndef ACC_OPS
`define ACC_OPS 200000
`endif

module tb_adders;
  localparam integer Q = 3329;

  reg clk = 1'b0, rst_n = 1'b0;
  always #1 clk = ~clk;

  // ---- modq_addsub, both REG_OUT settings ----------------------------------
  reg  [11:0] x = 12'd0, y = 12'd0;
  wire [11:0] sum0, diff0, sum1, diff1;
  modq_addsub #(.REG_OUT(0)) u_as0 (.clk(clk), .rst_n(rst_n), .x(x), .y(y), .sum(sum0), .diff(diff0));
  modq_addsub #(.REG_OUT(1)) u_as1 (.clk(clk), .rst_n(rst_n), .x(x), .y(y), .sum(sum1), .diff(diff1));

  integer errs = 0, n_as = 0, n_acc = 0;

  function [11:0] f_sum(input integer a, input integer b);
    f_sum = (a + b) % Q;
  endfunction
  function [11:0] f_diff(input integer a, input integer b);
    f_diff = (a - b + Q) % Q;
  endfunction

  // REG_OUT = 1 must show last clock's pair
  reg [11:0] px, py;
  reg        pv = 1'b0;
  always @(posedge clk) begin
    if (pv && (sum1 !== f_sum(px, py) || diff1 !== f_diff(px, py))) begin
      if (errs < 10) $display("REG_OUT=1 x=%0d y=%0d: sum %0d diff %0d", px, py, sum1, diff1);
      errs = errs + 1;
    end
    px <= x; py <= y; pv <= rst_n;
  end

  task as_check(input integer a, input integer b);
    begin
      @(negedge clk); x = a; y = b; #0.1;
      n_as = n_as + 1;
      if (sum0 !== f_sum(a, b) || diff0 !== f_diff(a, b)) begin
        if (errs < 10) $display("REG_OUT=0 x=%0d y=%0d: sum %0d diff %0d", a, b, sum0, diff0);
        errs = errs + 1;
      end
    end
  endtask

  task as_vec(input integer a, input integer b, input integer es, input integer ed);
    begin
      as_check(a, b);
      if ((es >= 0 && sum0 !== es) || (ed >= 0 && diff0 !== ed)) begin
        $display("vector x=%0d y=%0d: sum %0d (want %0d) diff %0d (want %0d)", a, b, sum0, es, diff0, ed);
        errs = errs + 1;
      end
    end
  endtask

  // ---- modq_acc ------------------------------------------------------------
  reg         ld = 1'b0, add = 1'b0;
  reg  [11:0] ay = 12'd0;
  wire [11:0] acc;
  modq_acc u_acc (.clk(clk), .rst_n(rst_n), .ld(ld), .add(add), .y(ay), .acc(acc));

  integer model = 0;

  // one clock of the accumulator, then compare with the model
  task acc_op(input l, input a, input integer v, input r);
    begin
      @(negedge clk); ld = l; add = a; ay = v; rst_n = ~r;
      if (r)      model = 0;
      else if (l) model = v;
      else if (a) model = (model + v) % Q;
      @(negedge clk); ld = 1'b0; add = 1'b0; rst_n = 1'b1;
      n_acc = n_acc + 1;
      if (acc !== model[11:0]) begin
        if (errs < 10) $display("acc ld=%b add=%b y=%0d rst=%b: got %0d, want %0d", l, a, v, r, acc, model);
        errs = errs + 1;
      end
    end
  endtask

  task acc_vec(input integer v0, input integer v1, input integer want);
    begin
      acc_op(1'b1, 1'b0, v0, 1'b0);
      acc_op(1'b0, 1'b1, v1, 1'b0);
      if (acc !== want) begin
        $display("acc vector ld %0d add %0d: got %0d, want %0d", v0, v1, acc, want);
        errs = errs + 1;
      end
    end
  endtask

  integer i, j, k;

  initial begin
    repeat (3) @(posedge clk);
    @(negedge clk) rst_n = 1'b1;

    // Spec 4 vector table (-1: not given)
    as_vec(3328, 1, 0, -1);
    as_vec(0, 1, -1, 3328);
    as_vec(3328, 3328, 3327, -1);
    as_vec(1000, 2000, -1, 2329);
    as_vec(2000, 1329, 0, -1);
    as_vec(1000, 2498, 169, 1831);
    as_vec(2000, 1000, 3000, 1000);
    // edges
    as_vec(0, 0, 0, 0);
    as_vec(0, 3328, 3328, 1);
    as_vec(3328, 0, 3328, 3328);

    // sweep: every x, every Y_STEP-th y, and always y = 3328
    for (i = 0; i < Q; i = i + 1) begin
      for (j = 0; j < Q; j = j + `Y_STEP) as_check(i, j);
      if ((Q - 1) % `Y_STEP != 0) as_check(i, Q - 1);
    end
    @(negedge clk);
    $display("modq_addsub: %0d pairs checked (REG_OUT 0 and 1)", n_as);

    // modq_acc: Spec 4 vectors
    acc_vec(1547, 55, 1602);
    acc_vec(65, 77, 142);
    acc_vec(1148, 2493, 312);
    acc_vec(362, 413, 775);
    acc_vec(3328, 1, 0);
    acc_vec(3328, 3328, 3327);
    // ld wins over add, both low holds, reset clears
    acc_op(1'b1, 1'b0, 100, 1'b0);
    acc_op(1'b1, 1'b1, 7, 1'b0);
    if (acc !== 7) begin $display("ld did not win over add"); errs = errs + 1; end
    acc_op(1'b0, 1'b0, 3000, 1'b0);
    if (acc !== 7) begin $display("acc did not hold"); errs = errs + 1; end
    acc_op(1'b0, 1'b1, 3000, 1'b1);
    if (acc !== 0) begin $display("reset did not clear acc"); errs = errs + 1; end
    // random operations; reset now and then
    for (k = 0; k < `ACC_OPS; k = k + 1)
      acc_op($urandom % 4 == 0, $urandom % 4 != 1, $urandom % Q, $urandom % 1000 == 0);
    $display("modq_acc: %0d operations checked", n_acc);

    if (errs != 0) $fatal(1, "FAILED: %0d errors", errs);
    $display("PASS");
    $finish;
  end
endmodule
