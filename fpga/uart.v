// 8N1 UART receiver and transmitter for the Tang Nano 20K emulator.
// FPGA-only code: lives in fpga/, never listed in info.yaml.

`default_nettype none

module uart_rx #(
    parameter CLK_HZ = 27_000_000,
    parameter BAUD   = 115_200
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       rx,
    output reg  [7:0] data,
    output reg        valid      // one-cycle pulse when data is new
);
  localparam integer CPB = (CLK_HZ + BAUD / 2) / BAUD;  // clocks per bit
  localparam integer W   = $clog2(CPB * 3 / 2 + 1);

  reg [1:0] sync;   // 2-FF synchronizer, idle high
  always @(posedge clk) sync <= rst ? 2'b11 : {sync[0], rx};
  wire rxs = sync[1];

  reg [1:0]   st;   // 0 idle, 1 data bits, 2 stop bit
  reg [W-1:0] cnt;
  reg [2:0]   bitn;
  reg [7:0]   sh;

  always @(posedge clk) begin
    valid <= 1'b0;
    if (rst) begin
      st <= 0; cnt <= 0; bitn <= 0; sh <= 0; data <= 0;
    end else case (st)
      0: if (!rxs) begin                  // start bit edge: wait 1.5 bits to mid of bit 0
           st <= 1; cnt <= CPB * 3 / 2 - 1; bitn <= 0;
         end
      1: if (cnt != 0) cnt <= cnt - 1;
         else begin
           sh <= {rxs, sh[7:1]};
           cnt <= CPB - 1;
           bitn <= bitn + 1;
           if (bitn == 7) st <= 2;
         end
      2: if (cnt != 0) cnt <= cnt - 1;
         else begin                       // middle of stop bit
           st <= 0;
           if (rxs) begin data <= sh; valid <= 1'b1; end   // drop framing errors
         end
      default: st <= 0;
    endcase
  end
endmodule

module uart_tx #(
    parameter CLK_HZ = 27_000_000,
    parameter BAUD   = 115_200
) (
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] data,
    input  wire       start,     // accepted only when !busy
    output reg        tx,
    output wire       busy
);
  localparam integer CPB = (CLK_HZ + BAUD / 2) / BAUD;
  localparam integer W   = $clog2(CPB + 1);

  reg [9:0]   sh;      // {stop, data, start}, shifted out LSB first
  reg [3:0]   left;    // bits remaining
  reg [W-1:0] cnt;
  assign busy = (left != 0) || (cnt != 0);   // stays busy for the whole stop bit

  always @(posedge clk) begin
    if (rst) begin
      tx <= 1'b1; sh <= 10'h3ff; left <= 0; cnt <= 0;
    end else if (cnt != 0) begin
      cnt <= cnt - 1;
    end else if (left != 0) begin
      tx <= sh[0];
      sh <= {1'b1, sh[9:1]};
      left <= left - 1;
      cnt <= CPB - 1;
    end else begin
      tx <= 1'b1;
      if (start) begin
        sh <= {1'b1, data, 1'b0};
        left <= 10;
      end
    end
  end
endmodule
