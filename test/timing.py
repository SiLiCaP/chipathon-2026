# SPDX-License-Identifier: Apache-2.0
"""Every timing number of the chip, in one place (see docs/host_timing.md).

Clock convention at the pins: in host clock k the host drives input word k
and samples uo_out (just before the rising edge that captures word k).
D is the number of clocks from a unit's first input word to its first byte.
"""

# Block latencies (from the block owners' READMEs; placeholders for now)
L_CAP = 1      # io_in_capture
L_MUL = 3      # modq_mul, start -> p_valid
PRE_LAT = 0    # Pre +/- modq_addsub REG_OUT
POST_LAT = 0   # Post +/- modq_addsub REG_OUT
L_PACK = 1     # io_out_pack, pair_valid -> first byte

# Clocks per unit (the input period the host must keep)
P_F = 3        # forward butterfly
P_I = 3        # inverse butterfly
P_B = 15       # basemul piece
P_S = 3        # scale coefficient (6 per output pair)

# Input offsets of each coefficient within its unit
OFFS_F = (0, 1)          # b, a
OFFS_I = (0, 1)          # b, a
OFFS_B = (0, 1, 2, 3)    # g1, f1, g0, f0
OFFS_S = (0,)            # c

# Core clock (offset from the unit's first coefficient on coeff_q) at which
# ntt_core raises pair_valid for that unit. For scale, the unit is the first
# coefficient of the pair (the pair completes with the second, 3 clocks later).
def core_timing(l_mul=L_MUL, pre_lat=PRE_LAT, post_lat=POST_LAT):
    """pair_valid offset per op (0 forward, 1 inverse, 2 basemul, 3 scale)."""
    return {
        0: 1 + l_mul + post_lat,      # forward: start at offset 1
        1: 2 + pre_lat + l_mul,       # inverse: start at offset 2 + PRE_LAT
        2: 14 + l_mul + 1,            # basemul: m5 starts at offset 14, acc adds 1
        3: P_S + 1 + l_mul,           # scale: second coefficient starts at offset 4
    }


PV_F, PV_I, PV_B, PV_S = (core_timing()[op] for op in range(4))

# Output delays at the pins
D_F = L_CAP + PV_F + L_PACK
D_I = L_CAP + PV_I + L_PACK
D_B = L_CAP + PV_B + L_PACK
D_S = L_CAP + PV_S + L_PACK

