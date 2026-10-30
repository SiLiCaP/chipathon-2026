// Tang Nano 20K emulator wrapper: runs the unmodified Tiny Tapeout top module from src/
// and lets a PC drive it over the onboard BL616 USB-UART (protocol in fpga/PROTOCOL.md).
// FPGA-only code: lives in fpga/, never listed in info.yaml.
//
// DUT clock: a fabric register toggled from the 27 MHz system clock. One DUT period is
// 2*div system clocks (high for div, low for div). DUT inputs change on the falling edge;
// outputs are sampled on the system clock edge that raises the DUT clock, i.e. just before
// the DUT sees its rising edge.

`default_nettype none

`ifndef TT_TOP
`define TT_TOP tt_um_ntt_engine
`endif

module tangnano20k_top #(
    parameter CLK_HZ = 27_000_000,
    parameter BAUD   = 115_200,
    parameter AW     = 13,            // 2^13 = 8192 stimulus words / captures
    parameter TO_W   = 22             // RX inter-byte timeout ~ 2^22 / 27 MHz = 155 ms
) (
    input  wire       clk,            // 27 MHz crystal
    input  wire       uart_rx,        // from BL616 (PC -> FPGA)
    output wire       uart_tx,        // to BL616   (FPGA -> PC)
    output wire [5:0] led             // active low
);
  localparam [31:0] MAGIC = 32'h3145_5454;   // bytes on the wire: "TTE1"
  localparam [AW:0] DEPTH = 1 << AW;

  // ---------------- power-on reset ----------------
  reg [7:0] por_cnt = 8'd0;
  wire rst = (por_cnt != 8'hff);
  always @(posedge clk) if (rst) por_cnt <= por_cnt + 8'd1;

  // ---------------- UART ----------------
  wire [7:0] rx_data;
  wire       rx_valid;
  reg  [7:0] tx_data;
  reg        tx_start;
  wire       tx_busy;

  uart_rx #(.CLK_HZ(CLK_HZ), .BAUD(BAUD)) u_rx (
      .clk(clk), .rst(rst), .rx(uart_rx), .data(rx_data), .valid(rx_valid));
  uart_tx #(.CLK_HZ(CLK_HZ), .BAUD(BAUD)) u_tx (
      .clk(clk), .rst(rst), .data(tx_data), .start(tx_start), .tx(uart_tx), .busy(tx_busy));

  // ---------------- DUT ----------------
  reg        dut_clk;
  reg        dut_rst_n;
  reg  [7:0] dut_ui;
  reg  [7:0] dut_uio;
  wire [7:0] dut_uo;
  wire [7:0] dut_uio_out;
  wire [7:0] dut_uio_oe;

  // The DUT clock goes onto a global clock network: on general routing its skew (up to 1.6 ns
  // measured) breaks hold timing in larger designs such as the gate-level netlist.
  wire       dut_clk_g;
`ifdef SYNTHESIS
  BUFG u_dut_clk_bufg (.I(dut_clk), .O(dut_clk_g));
`else
  assign dut_clk_g = dut_clk;
`endif

  `TT_TOP dut (
      .ui_in  (dut_ui),
      .uo_out (dut_uo),
      .uio_in (dut_uio),
      .uio_out(dut_uio_out),
      .uio_oe (dut_uio_oe),
      .ena    (1'b1),
      .clk    (dut_clk_g),
      .rst_n  (dut_rst_n)
  );

  wire [23:0] dut_out = {dut_uio_oe, dut_uio_out, dut_uo};

  // ---------------- stimulus and capture RAMs (inferred block RAM) ----------------
  reg [15:0] stim_mem [0:(1<<AW)-1];
  reg        stim_we;
  reg [AW-1:0] stim_waddr, stim_raddr;
  reg [15:0] stim_wdata, stim_rdata;
  always @(posedge clk) begin
    if (stim_we) stim_mem[stim_waddr] <= stim_wdata;
    stim_rdata <= stim_mem[stim_raddr];
  end

  reg [23:0] cap_mem [0:(1<<AW)-1];
  reg        cap_we;
  reg [AW-1:0] cap_waddr, cap_raddr;
  reg [23:0] cap_wdata, cap_rdata;
  always @(posedge clk) begin
    if (cap_we) cap_mem[cap_waddr] <= cap_wdata;
    cap_rdata <= cap_mem[cap_raddr];
  end

  // ---------------- command processor ----------------
  localparam [7:0] OP_PING = 8'h00, OP_RESET = 8'h01, OP_SET_IN = 8'h02, OP_STEP = 8'h03,
                   OP_SET_DIV = 8'h04, OP_LOAD = 8'h10, OP_RUN = 8'h11, OP_READ_CAP = 8'h12;

  localparam [3:0] S_IDLE = 0, S_ARGS = 1, S_EXEC = 2, S_LOAD_LO = 3, S_LOAD_HI = 4,
                   S_PULSE = 5, S_RUN_PRE1 = 6, S_RUN_PRE2 = 7, S_RUN = 8,
                   S_RC_READ = 9, S_RC_WAIT1 = 10, S_RC_WAIT2 = 11, S_SEND = 12;

  reg [3:0]  st, ret;
  reg [7:0]  op;
  reg [7:0]  arg [0:3];
  reg [2:0]  nargs, ai;
  wire [15:0] u16a = {arg[1], arg[0]};
  wire [15:0] u16b = {arg[3], arg[2]};

  reg [7:0]  txb [0:5];      // response buffer
  reg [2:0]  txn, txi;

  reg [7:0]  div;            // half DUT period in system clocks, >= 1
  reg [8:0]  ph;             // phase inside a DUT period, 0 .. 2*div-1
  wire [8:0] ph_last = {div, 1'b0} - 9'd1;
  reg [15:0] cnt;            // pulses left / words done
  reg [15:0] len;
  reg [AW-1:0] base;
  reg        first;
  reg [7:0]  load_lo;
  reg [TO_W-1:0] idle_t;     // RX inter-byte timer for half-received commands

  function [2:0] args_for(input [7:0] o);
    case (o)
      OP_PING:     args_for = 3'd0;
      OP_RESET:    args_for = 3'd2;
      OP_SET_IN:   args_for = 3'd2;
      OP_STEP:     args_for = 3'd2;
      OP_SET_DIV:  args_for = 3'd1;
      OP_LOAD:     args_for = 3'd4;
      OP_RUN:      args_for = 3'd2;
      OP_READ_CAP: args_for = 3'd4;
      default:     args_for = 3'd7;   // unknown
    endcase
  endfunction

  task send_ok;   // queue "A5 00" and go to S_SEND, then IDLE
    begin
      txb[0] <= 8'hA5; txb[1] <= 8'h00; txn <= 3'd2; txi <= 3'd0;
      ret <= S_IDLE; st <= S_SEND;
    end
  endtask

  always @(posedge clk) begin
    stim_we  <= 1'b0;
    cap_we   <= 1'b0;
    tx_start <= 1'b0;

    if (rst) begin
      st <= S_IDLE; ret <= S_IDLE; op <= 0; nargs <= 0; ai <= 0;
      txn <= 0; txi <= 0; tx_data <= 0;
      div <= 8'd1; ph <= 0; cnt <= 0; len <= 0; base <= 0; first <= 0; load_lo <= 0;
      idle_t <= 0;
      dut_clk <= 1'b0; dut_rst_n <= 1'b0; dut_ui <= 0; dut_uio <= 0;
      stim_waddr <= 0; stim_raddr <= 0; stim_wdata <= 0;
      cap_waddr <= 0; cap_raddr <= 0; cap_wdata <= 0;
    end else begin
      // abandon a half-received command if the host goes quiet
      if (rx_valid || st == S_IDLE) idle_t <= 0;
      else if (!(&idle_t)) idle_t <= idle_t + 1'b1;

      case (st)
        S_IDLE: if (rx_valid) begin
          op <= rx_data;
          nargs <= args_for(rx_data);
          ai <= 0;
          if (args_for(rx_data) == 3'd7) begin
            txb[0] <= 8'hA5; txb[1] <= 8'h01; txn <= 3'd2; txi <= 3'd0;   // unknown opcode
            ret <= S_IDLE; st <= S_SEND;
          end else if (args_for(rx_data) == 3'd0) st <= S_EXEC;
          else st <= S_ARGS;
        end

        S_ARGS: if (rx_valid) begin
          arg[ai] <= rx_data;
          ai <= ai + 1'b1;
          if (ai == nargs - 1'b1) st <= S_EXEC;
        end else if (&idle_t) st <= S_IDLE;

        S_EXEC: case (op)
          OP_PING: begin
            txb[0] <= 8'hA5; txb[1] <= 8'h00;
            txb[2] <= MAGIC[7:0]; txb[3] <= MAGIC[15:8]; txb[4] <= MAGIC[23:16]; txb[5] <= MAGIC[31:24];
            txn <= 3'd6; txi <= 3'd0; ret <= S_IDLE; st <= S_SEND;
          end
          OP_RESET: begin
            dut_rst_n <= 1'b0; cnt <= u16a; ph <= 0; dut_clk <= 1'b0; st <= S_PULSE;
          end
          OP_SET_IN: begin
            dut_ui <= arg[0]; dut_uio <= arg[1]; send_ok;
          end
          OP_STEP: begin
            cnt <= u16a; ph <= 0; dut_clk <= 1'b0; st <= S_PULSE;
          end
          OP_SET_DIV: begin
            div <= (arg[0] == 8'd0) ? 8'd1 : arg[0]; send_ok;
          end
          OP_LOAD: begin
            base <= u16a[AW-1:0]; len <= u16b; cnt <= 0;
            if (u16b == 0) send_ok; else st <= S_LOAD_LO;
          end
          OP_RUN: begin
            len <= u16a; cnt <= 0;
            if (u16a == 0) send_ok; else st <= S_RUN_PRE1;
          end
          OP_READ_CAP: begin
            base <= u16a[AW-1:0]; len <= u16b; cnt <= 0;
            txb[0] <= 8'hA5; txb[1] <= 8'h00; txn <= 3'd2; txi <= 3'd0;
            ret <= S_RC_READ; st <= S_SEND;
          end
          default: st <= S_IDLE;
        endcase

        // ---- LOAD: little-endian u16 words into stimulus RAM ----
        S_LOAD_LO: if (rx_valid) begin
          load_lo <= rx_data; st <= S_LOAD_HI;
        end else if (&idle_t) st <= S_IDLE;

        S_LOAD_HI: if (rx_valid) begin
          stim_we <= 1'b1;
          stim_waddr <= base + cnt[AW-1:0];
          stim_wdata <= {rx_data, load_lo};
          cnt <= cnt + 1'b1;
          if (cnt + 1'b1 == len) send_ok; else st <= S_LOAD_LO;
        end else if (&idle_t) st <= S_IDLE;

        // ---- RESET / STEP: cnt full DUT clock periods with inputs held ----
        S_PULSE: if (cnt == 0) begin
          if (op == OP_RESET) begin
            dut_rst_n <= 1'b1; send_ok;
          end else begin
            txb[0] <= 8'hA5; txb[1] <= 8'h00;
            txb[2] <= dut_uo; txb[3] <= dut_uio_out; txb[4] <= dut_uio_oe;
            txn <= 3'd5; txi <= 3'd0; ret <= S_IDLE; st <= S_SEND;
          end
        end else begin
          if (ph == 0) dut_clk <= 1'b1;
          if (ph == {1'b0, div}) dut_clk <= 1'b0;
          if (ph == ph_last) begin ph <= 0; cnt <= cnt - 1'b1; end
          else ph <= ph + 1'b1;
        end

        // ---- RUN: play stim[0..len-1] at full DUT speed, capture into cap[0..len-1] ----
        S_RUN_PRE1: begin stim_raddr <= 0; first <= 1'b1; st <= S_RUN_PRE2; end
        S_RUN_PRE2: begin ph <= {1'b0, div}; st <= S_RUN; end   // stim_rdata = stim[0] next cycle

        S_RUN: begin
          ph <= (ph == ph_last) ? 9'd0 : ph + 1'b1;
          if (ph == 0) begin                        // rising edge: sample outputs first
            cap_we <= 1'b1;
            cap_waddr <= cnt[AW-1:0];
            cap_wdata <= dut_out;
            dut_clk <= 1'b1;
          end
          if (ph == {1'b0, div}) begin              // falling edge: next inputs
            dut_clk <= 1'b0;
            if (first) begin
              first <= 1'b0;
              {dut_uio, dut_ui} <= stim_rdata;
              stim_raddr <= 1;
            end else if (cnt + 1'b1 == len) begin
              send_ok;
            end else begin
              {dut_uio, dut_ui} <= stim_rdata;
              cnt <= cnt + 1'b1;
              stim_raddr <= cnt[AW-1:0] + 2'd2;
            end
          end
        end

        // ---- READ_CAP: 3 bytes per capture {uo_out, uio_out, uio_oe} ----
        S_RC_READ: if (cnt == len) st <= S_IDLE;
          else begin cap_raddr <= base + cnt[AW-1:0]; st <= S_RC_WAIT1; end
        S_RC_WAIT1: st <= S_RC_WAIT2;
        S_RC_WAIT2: begin
          txb[0] <= cap_rdata[7:0]; txb[1] <= cap_rdata[15:8]; txb[2] <= cap_rdata[23:16];
          txn <= 3'd3; txi <= 3'd0; cnt <= cnt + 1'b1;
          ret <= S_RC_READ; st <= S_SEND;
        end

        // ---- send txb[0..txn-1], then go to ret ----
        S_SEND: if (tx_start) begin
          // start was issued last cycle; uart_tx raises busy this cycle
        end else if (!tx_busy) begin
          if (txi == txn) st <= ret;
          else begin
            tx_data <= txb[txi]; tx_start <= 1'b1; txi <= txi + 1'b1;
          end
        end

        default: st <= S_IDLE;
      endcase
    end
  end

  // ---------------- LEDs (active low) ----------------
  reg [24:0] hb;
  always @(posedge clk) hb <= rst ? 25'd0 : hb + 1'b1;
  reg [21:0] rx_led;
  always @(posedge clk)
    if (rst) rx_led <= 0;
    else if (rx_valid) rx_led <= {22{1'b1}};
    else if (rx_led != 0) rx_led <= rx_led - 1'b1;

  assign led = ~{3'b000,
                 rx_led != 0,         // LED2: UART traffic
                 st != S_IDLE,        // LED1: command / run in progress
                 hb[24]};             // LED0: heartbeat ~0.8 Hz
endmodule
