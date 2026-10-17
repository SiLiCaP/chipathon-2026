// Testbench for modmul_kred.
// Mode 1: checks the raw function      r == a*b*2197 mod 3329
// Mode 2: checks the NTT usage model   feed b' = b*944 mod 3329 (pre-scaled
//         twiddle), expect             r == a*b mod 3329
`timescale 1ns/1ps
module tb_kred;
    localparam integer Q      = 3329;
    localparam integer K3     = 2197;   // k^3
    localparam integer K3_INV = 944;    // k^-3 mod Q
    parameter  integer N      = 500000; // vectors per mode

    reg clk = 0, rst_n = 0, vin = 0;
    reg  [11:0] a, b;
    wire [11:0] r;
    wire        vout;

    modmul_kred dut (.clk(clk), .rst_n(rst_n), .in_valid(vin),
                     .a(a), .b(b), .out_valid(vout), .r(r));

    always #5 clk = ~clk;

    // FIFO of expected results (latency 3 < depth 8)
    reg [11:0] exp_q [0:7];
    integer wp = 0, rp = 0, errs = 0, checked = 0, i, mode;
    reg [31:0] fa, fb, bs, e;

    task drive(input [31:0] da, input [31:0] db, input [31:0] expv);
        begin
            @(negedge clk);
            a = da; b = db; vin = 1;
            exp_q[wp] = expv; wp = (wp + 1) % 8;
        end
    endtask

    initial begin
        #12 rst_n = 1;
        for (mode = 1; mode <= 2; mode = mode + 1)
            for (i = 0; i < N; i = i + 1) begin
                case (i)                         // corner cases first
                    0: begin fa = 0;   fb = 0;   end
                    1: begin fa = Q-1; fb = Q-1; end
                    2: begin fa = 1;   fb = 1;   end
                    3: begin fa = Q-1; fb = 1;   end
                    4: begin fa = 255; fb = 255; end
                    default: begin fa = $urandom % Q; fb = $urandom % Q; end
                endcase
                if (mode == 1) drive(fa, fb, (((fa * fb) % Q) * K3) % Q);  // split to avoid 32-bit overflow
                else begin
                    bs = (fb * K3_INV) % Q;      // what the twiddle ROM holds
                    drive(fa, bs, (fa * fb) % Q);
                end
            end
        @(negedge clk) vin = 0;
        repeat (5) @(negedge clk);
        $display("checked=%0d errors=%0d", checked, errs);
        $finish;
    end

    // compare on negedge, when DUT outputs are stable
    always @(negedge clk) if (vout) begin
        if (r !== exp_q[rp]) begin
            errs = errs + 1;
            if (errs < 10) $display("ERR got %0d expected %0d", r, exp_q[rp]);
        end
        rp = (rp + 1) % 8; checked = checked + 1;
    end
endmodule