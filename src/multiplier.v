// =============================================================================
// modmul_kred : K-RED modular multiplier for Q = 3329 (Kyber / ML-KEM NTT)
// =============================================================================
//
// WHAT IT COMPUTES
//   r = (a * b * k^3) mod Q ,   r fully reduced to [0, Q)
//   with Q = 3329 = k * 2^m + 1,  k = 13,  m = 8,  k^3 = 2197.
//
//   The extra factor k^3 is removed for free by storing pre-scaled twiddles:
//       twiddle_rom[i] = zeta[i] * k^-3 mod Q = zeta[i] * 944 mod Q
//   then  modmul_kred(a, twiddle_rom[i]) = a * zeta[i] mod Q   exactly.
//   (944 * 2197 = 2073968 = 623*3329 + 1)
//
// THE K-RED IDEA
//   Because Q = k*2^8 + 1, we have      k * 2^8 = Q - 1 = -1  (mod Q).
//   Split any number C into a low byte and the rest: C = C1*2^8 + C0. Then
//       k*C = C1*(k*2^8) + k*C0 = k*C0 - C1   (mod Q)
//   i.e. one K-RED step replaces "multiply by k" + "mod Q" with
//   one tiny constant multiply (13 = 8+4+1 -> shifts + adds) and a subtract.
//   Each step shrinks C by ~8 bits but multiplies the residue by k.
//
// PIPELINE (3 stages, 1 result per clock, latency 3)
//   S1: C = a * b                       24-bit unsigned, C < Q^2 < 2^24
//   S2: K-RED twice (bytes C2|C1|C0):
//         t = 169*C0 - 13*C1 + C2       = k^2 * C   (mod Q)
//         range of t: [-3315, 43264]    -> 17-bit signed
//   S3: K-RED once more on t (t = t1*2^8 + t0, t1 signed):
//         u = 13*t0 - t1                = k^3 * C   (mod Q)
//         range of u: [-169, 3328]      -> only negative case needs fixing
//         r = (u < 0) ? u + Q : u       -> [0, Q)
//   All ranges verified exhaustively for every C in [0, Q^2).
//
// COST: one 12x12 multiplier + ~8 narrow adders (8..17 bit) + 1 mux.
//       No Barrett constant multiplier, no comparator chain.
//
// NOTE: works ONLY for Q = 3329 (constants are hardwired). Inputs must be
//       < Q, otherwise the range proofs above do not hold.
// =============================================================================
module modmul_kred (
    input  wire        clk,
    input  wire        rst_n,      // async active-low reset
    input  wire        in_valid,
    input  wire [11:0] a,          // data operand,        0 <= a < 3329
    input  wire [11:0] b,          // pre-scaled twiddle,  0 <= b < 3329
    output reg         out_valid,
    output reg  [11:0] r           // a*b*2197 mod 3329,   0 <= r < 3329
);
    localparam [11:0] Q = 12'd3329;

    // =========================================================================
    // Stage 1 : plain 12x12 product.  The only "real" multiplier in the design.
    // Max value (Q-1)^2 = 11 075 584 < 2^24, so 24 bits are enough.
    // =========================================================================
    reg  [23:0] c_s1;
    reg         v_s1;

    always @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            c_s1 <= 24'd0;
            v_s1 <= 1'b0;
        end else begin
            c_s1 <= a * b;
            v_s1 <= in_valid;
        end

    // =========================================================================
    // Stage 2 : double K-RED   t = k^2*C0 - k*C1 + C2   (= k^2 * C mod Q)
    //
    // Derivation: C = C2*2^16 + C1*2^8 + C0
    //   k^2*C = C2*(k*2^8)^2 + k*C1*(k*2^8) + k^2*C0
    //         = C2*(+1)      + k*C1*(-1)    + k^2*C0      (mod Q)
    // =========================================================================
    wire [7:0] c0 = c_s1[7:0];      // 0..255
    wire [7:0] c1 = c_s1[15:8];     // 0..255
    wire [7:0] c2 = c_s1[23:16];    // 0..169 (because C < Q^2)

    // 169*C0 = (128 + 32 + 8 + 1)*C0   -> max 43 095, 16 bits
    wire [15:0] c0_x169 = ({c0, 7'd0}) + ({c0, 5'd0}) + ({c0, 3'd0}) + c0;

    // 13*C1  = (8 + 4 + 1)*C1          -> max 3 315, 12 bits
    wire [11:0] c1_x13  = ({c1, 3'd0}) + ({c1, 2'd0}) + c1;

    // t = 169*C0 + C2 - 13*C1, true range [-3315, 43264].
    // Computed in 17-bit two's complement: unsigned add/sub that wraps is
    // exactly signed arithmetic as long as the true result fits in 17 bits
    // signed (-65536 .. 65535), which it does.
    wire [16:0] t_w = {1'b0, c0_x169} + {9'd0, c2} - {5'd0, c1_x13};

    reg  [16:0] t_s2;               // interpreted as SIGNED 17-bit
    reg         v_s2;

    always @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            t_s2 <= 17'd0;
            v_s2 <= 1'b0;
        end else begin
            t_s2 <= t_w;
            v_s2 <= v_s1;
        end

    // =========================================================================
    // Stage 3 : third K-RED + final correction
    //
    // Split signed t as t = t1*2^8 + t0, with
    //   t0 = t[7:0]            unsigned 0..255
    //   t1 = t >>> 8 (floor)   signed   -13..169   (= bits t[16:8], 9-bit signed)
    // The K-RED identity holds for negative numbers too, since it is pure
    // integer algebra; floor-split just keeps t0 non-negative.
    //
    //   u = 13*t0 - t1,  range [-169, 3328]
    // Upper bound 13*255 + 13 = 3328 = Q-1 -> never >= Q, so ONLY the
    // negative case needs a correction (+Q). One adder + one mux.
    // =========================================================================
    wire [7:0]  t0     = t_s2[7:0];
    wire [8:0]  t1     = t_s2[16:8];                    // 9-bit signed

    wire [11:0] t0_x13 = ({t0, 3'd0}) + ({t0, 2'd0}) + t0;   // 0..3315

    // 14-bit two's complement: zero-extend 13*t0, sign-extend t1
    wire [13:0] u      = {2'b00, t0_x13} - {{5{t1[8]}}, t1};

    // u[13] is the sign bit. If negative, add Q (result lands in [3160, 3328]).
    wire [13:0] u_fix  = u + {2'b00, Q};
    wire [11:0] r_w    = u[13] ? u_fix[11:0] : u[11:0];

    always @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            r         <= 12'd0;
            out_valid <= 1'b0;
        end else begin
            r         <= r_w;
            out_valid <= v_s2;
        end

    // -------------------------------------------------------------------------
    // Timing note: stages 2 and 3 are only narrow adders. If your clock target
    // allows, merge them into one stage (latency 2). If the 12x12 multiplier
    // in stage 1 is the critical path, split it (e.g. register partial
    // products) and enable retiming in synthesis.
    // -------------------------------------------------------------------------
endmodule