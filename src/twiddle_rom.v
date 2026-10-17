/*
 * Twiddle factor ROM for the Kyber / ML-KEM NTT (q = 3329, n = 256).
 *
 * Entry k holds zeta^brv7(k) mod q, where zeta = 17 is the primitive 256th root
 * of unity mod 3329 and brv7(k) reverses the 7 bits of k. This is the zetas
 * table from FIPS 203 Appendix A, in the normal domain (not Montgomery form).
 *
 * The forward NTT uses entries 1..127 in order. Entry 0 is 1 and is not used.
 * Purely combinational: twiddle follows addr with no clock.
 */
module twiddle_rom (
    input  wire [6:0]  addr,
    output reg  [11:0] twiddle
);

  always @(*) begin
    case (addr)
      7'd0  : twiddle = 12'd1;
      7'd1  : twiddle = 12'd1729;
      7'd2  : twiddle = 12'd2580;
      7'd3  : twiddle = 12'd3289;
      7'd4  : twiddle = 12'd2642;
      7'd5  : twiddle = 12'd630;
      7'd6  : twiddle = 12'd1897;
      7'd7  : twiddle = 12'd848;
      7'd8  : twiddle = 12'd1062;
      7'd9  : twiddle = 12'd1919;
      7'd10 : twiddle = 12'd193;
      7'd11 : twiddle = 12'd797;
      7'd12 : twiddle = 12'd2786;
      7'd13 : twiddle = 12'd3260;
      7'd14 : twiddle = 12'd569;
      7'd15 : twiddle = 12'd1746;
      7'd16 : twiddle = 12'd296;
      7'd17 : twiddle = 12'd2447;
      7'd18 : twiddle = 12'd1339;
      7'd19 : twiddle = 12'd1476;
      7'd20 : twiddle = 12'd3046;
      7'd21 : twiddle = 12'd56;
      7'd22 : twiddle = 12'd2240;
      7'd23 : twiddle = 12'd1333;
      7'd24 : twiddle = 12'd1426;
      7'd25 : twiddle = 12'd2094;
      7'd26 : twiddle = 12'd535;
      7'd27 : twiddle = 12'd2882;
      7'd28 : twiddle = 12'd2393;
      7'd29 : twiddle = 12'd2879;
      7'd30 : twiddle = 12'd1974;
      7'd31 : twiddle = 12'd821;
      7'd32 : twiddle = 12'd289;
      7'd33 : twiddle = 12'd331;
      7'd34 : twiddle = 12'd3253;
      7'd35 : twiddle = 12'd1756;
      7'd36 : twiddle = 12'd1197;
      7'd37 : twiddle = 12'd2304;
      7'd38 : twiddle = 12'd2277;
      7'd39 : twiddle = 12'd2055;
      7'd40 : twiddle = 12'd650;
      7'd41 : twiddle = 12'd1977;
      7'd42 : twiddle = 12'd2513;
      7'd43 : twiddle = 12'd632;
      7'd44 : twiddle = 12'd2865;
      7'd45 : twiddle = 12'd33;
      7'd46 : twiddle = 12'd1320;
      7'd47 : twiddle = 12'd1915;
      7'd48 : twiddle = 12'd2319;
      7'd49 : twiddle = 12'd1435;
      7'd50 : twiddle = 12'd807;
      7'd51 : twiddle = 12'd452;
      7'd52 : twiddle = 12'd1438;
      7'd53 : twiddle = 12'd2868;
      7'd54 : twiddle = 12'd1534;
      7'd55 : twiddle = 12'd2402;
      7'd56 : twiddle = 12'd2647;
      7'd57 : twiddle = 12'd2617;
      7'd58 : twiddle = 12'd1481;
      7'd59 : twiddle = 12'd648;
      7'd60 : twiddle = 12'd2474;
      7'd61 : twiddle = 12'd3110;
      7'd62 : twiddle = 12'd1227;
      7'd63 : twiddle = 12'd910;
      7'd64 : twiddle = 12'd17;
      7'd65 : twiddle = 12'd2761;
      7'd66 : twiddle = 12'd583;
      7'd67 : twiddle = 12'd2649;
      7'd68 : twiddle = 12'd1637;
      7'd69 : twiddle = 12'd723;
      7'd70 : twiddle = 12'd2288;
      7'd71 : twiddle = 12'd1100;
      7'd72 : twiddle = 12'd1409;
      7'd73 : twiddle = 12'd2662;
      7'd74 : twiddle = 12'd3281;
      7'd75 : twiddle = 12'd233;
      7'd76 : twiddle = 12'd756;
      7'd77 : twiddle = 12'd2156;
      7'd78 : twiddle = 12'd3015;
      7'd79 : twiddle = 12'd3050;
      7'd80 : twiddle = 12'd1703;
      7'd81 : twiddle = 12'd1651;
      7'd82 : twiddle = 12'd2789;
      7'd83 : twiddle = 12'd1789;
      7'd84 : twiddle = 12'd1847;
      7'd85 : twiddle = 12'd952;
      7'd86 : twiddle = 12'd1461;
      7'd87 : twiddle = 12'd2687;
      7'd88 : twiddle = 12'd939;
      7'd89 : twiddle = 12'd2308;
      7'd90 : twiddle = 12'd2437;
      7'd91 : twiddle = 12'd2388;
      7'd92 : twiddle = 12'd733;
      7'd93 : twiddle = 12'd2337;
      7'd94 : twiddle = 12'd268;
      7'd95 : twiddle = 12'd641;
      7'd96 : twiddle = 12'd1584;
      7'd97 : twiddle = 12'd2298;
      7'd98 : twiddle = 12'd2037;
      7'd99 : twiddle = 12'd3220;
      7'd100: twiddle = 12'd375;
      7'd101: twiddle = 12'd2549;
      7'd102: twiddle = 12'd2090;
      7'd103: twiddle = 12'd1645;
      7'd104: twiddle = 12'd1063;
      7'd105: twiddle = 12'd319;
      7'd106: twiddle = 12'd2773;
      7'd107: twiddle = 12'd757;
      7'd108: twiddle = 12'd2099;
      7'd109: twiddle = 12'd561;
      7'd110: twiddle = 12'd2466;
      7'd111: twiddle = 12'd2594;
      7'd112: twiddle = 12'd2804;
      7'd113: twiddle = 12'd1092;
      7'd114: twiddle = 12'd403;
      7'd115: twiddle = 12'd1026;
      7'd116: twiddle = 12'd1143;
      7'd117: twiddle = 12'd2150;
      7'd118: twiddle = 12'd2775;
      7'd119: twiddle = 12'd886;
      7'd120: twiddle = 12'd1722;
      7'd121: twiddle = 12'd1212;
      7'd122: twiddle = 12'd1874;
      7'd123: twiddle = 12'd1029;
      7'd124: twiddle = 12'd2110;
      7'd125: twiddle = 12'd2935;
      7'd126: twiddle = 12'd885;
      7'd127: twiddle = 12'd2154;
      default: twiddle = 12'd0;
    endcase
  end

endmodule
