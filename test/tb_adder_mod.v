`default_nettype none
`timescale 1ns / 1ps

// Standalone testbench for adder_mod (r = (a + b) mod N, a,b < N).
// adder_mod is combinational, so the bench wraps it in input/output
// registers (1 entry per clock) to count cycles for a 256-entry stream.
module tb_adder_mod ();

  localparam W = 8;
  localparam [W-1:0] N = 200;
  localparam ENTRIES = 256;

  initial begin
    $dumpfile("tb_adder_mod.vcd");
    $dumpvars(0, tb_adder_mod);
  end

  reg clk = 0;
  always #5 clk = ~clk;  // 100 MHz

  // ---- Directed / exhaustive section: drive the DUT directly ----
  reg  [W-1:0] da, db;
  wire [W-1:0] dr;
  adder_mod #(.W(W), .N(N)) dut_direct (.a(da), .b(db), .r(dr));

  integer errors = 0, checks = 0;

  task check(input [W-1:0] a, input [W-1:0] b);
    reg [W-1:0] exp;
    begin
      da = a; db = b; #1;
      exp = (32'(a) + 32'(b)) % 32'(N);
      checks = checks + 1;
      if (dr !== exp) begin
        errors = errors + 1;
        $display("FAIL a=%0d b=%0d got=%0d exp=%0d", a, b, dr, exp);
      end
    end
  endtask

  integer i, j;

  // ---- Pipelined section: in-reg -> adder_mod -> out-reg ----
  reg  [W-1:0] ra, rb;
  wire [W-1:0] pr;
  reg  [W-1:0] rr;
  reg          in_valid = 0, out_valid = 0;
  adder_mod #(.W(W), .N(N)) dut_pipe (.a(ra), .b(rb), .r(pr));

  reg [W-1:0] ma [0:ENTRIES-1];
  reg [W-1:0] mb [0:ENTRIES-1];
  integer issue_idx = 0, ret_idx = 0, pipe_errors = 0;
  integer cycle = 0, first_issue_cycle = -1, first_ret_cycle = -1, last_ret_cycle = -1;
  reg running = 0;

  always @(posedge clk) begin
    if (running) begin
      cycle <= cycle + 1;
      // input stage
      if (issue_idx < ENTRIES) begin
        ra <= ma[issue_idx]; rb <= mb[issue_idx];
        in_valid <= 1;
        if (first_issue_cycle < 0) first_issue_cycle <= cycle;
        issue_idx <= issue_idx + 1;
      end else in_valid <= 0;
      // output stage
      rr <= pr;
      out_valid <= in_valid;
      if (out_valid) begin
        if (first_ret_cycle < 0) first_ret_cycle <= cycle;
        last_ret_cycle <= cycle;
        if (rr !== (32'(ma[ret_idx]) + 32'(mb[ret_idx])) % 32'(N)) begin
          pipe_errors <= pipe_errors + 1;
          $display("PIPE FAIL idx=%0d a=%0d b=%0d got=%0d", ret_idx, ma[ret_idx], mb[ret_idx], rr);
        end
        ret_idx <= ret_idx + 1;
      end
    end
  end

  initial begin
    // Edge cases
    check(0, 0);
    check(0, N-1);
    check(N-1, 0);
    check(1, N-1);          // sum == N  -> 0
    check(N-1, N-1);        // max sum 2N-2
    check(N-2, 1);          // sum == N-1 (no wrap)
    check(N-2, 2);          // sum == N
    check(N-2, 3);          // sum == N+1
    check(100, 100);        // sum == N
    check(100, 99);         // sum == N-1
    check(100, 101);        // sum == N+1
    check(127, 128);        // MSB boundary
    check(128, 71);         // sum == N-1 across MSB
    $display("edge cases done: %0d checks, %0d errors", checks, errors);

    // Exhaustive over all valid inputs
    for (i = 0; i < N; i = i + 1)
      for (j = 0; j < N; j = j + 1)
        check(i, j);
    $display("exhaustive done: %0d checks, %0d errors", checks, errors);

    // 256-entry stream: first a few are corner cases, rest random
    for (i = 0; i < ENTRIES; i = i + 1) begin
      ma[i] = $urandom % N;
      mb[i] = $urandom % N;
    end
    ma[0] = 0;     mb[0] = 0;
    ma[1] = N-1;   mb[1] = N-1;
    ma[2] = 1;     mb[2] = N-1;
    ma[3] = N-1;   mb[3] = 1;

    @(negedge clk);
    running = 1;
    wait (ret_idx == ENTRIES);
    @(negedge clk);
    running = 0;

    $display("-----------------------------------------------");
    $display("stream: %0d entries, mismatches: %0d", ENTRIES, pipe_errors);
    $display("first issue cycle %0d, first result cycle %0d (latency %0d cycles)",
             first_issue_cycle, first_ret_cycle, first_ret_cycle - first_issue_cycle);
    $display("last result cycle %0d; total cycles issue->last result: %0d (%0d ns @10 ns clk)",
             last_ret_cycle, last_ret_cycle - first_issue_cycle + 1,
             (last_ret_cycle - first_issue_cycle + 1) * 10);
    $display("throughput: 1 entry/cycle");
    if (errors + pipe_errors == 0) $display("RESULT: PASS");
    else $display("RESULT: FAIL");
    $finish;
  end

endmodule
