#!/usr/bin/env python3
"""Gate-level clock-gate -> clock-enable conversion for the FPGA emulation (stdlib only).

usage: gl_icg_to_ce.py in.nl.v out.v

The FPGA fabric cannot gate a clock glitch-free, so the sky130 integrated clock gates
(dlclkp: GCLK = CLK & latch(GATE)) are removed.  Every flop that sat on a gated clock (directly
or through non-inverting clock buffers) gets the ICG's root clock instead and a recirculating
mux on D (D' = GATE ? D : Q).  For synchronous GATE this is cycle-exact: the latch holds GATE as
seen just before the rising edge, which is the GATE value the enable flop samples.
Everything else in the netlist is copied untouched."""
import re, sys

INST = re.compile(r'^[ \t]*(sky130_fd_sc_hd__\w+)[ \t]+(\\\S+|\w+)[ \t]*\((.*?)\);', re.S | re.M)
PIN = re.compile(r'\.(\w+)\(\s*(\\\S+|[^\s()]+(?:\s*\[[^\]]*\])?)\s*\)')
BUF = re.compile(r'sky130_fd_sc_hd__(clkbuf|buf|dlygate\w*|dlymetal\w*)_\d+$')
FLOP_D = re.compile(r'sky130_fd_sc_hd__dfxtp_\d+$')

def esc(n):
    return n + ' ' if n.startswith('\\') else n

def main(src, dst):
    text = open(src).read()
    insts = [(m, m.group(1), m.group(2), dict(PIN.findall(m.group(3)))) for m in INST.finditer(text)]
    drv = {}                               # net -> (cell, pins) of a buffer/ICG driving it
    for m, cell, name, pins in insts:
        if BUF.match(cell) and 'X' in pins: drv[pins['X']] = (cell, pins, 'A')
        elif cell.startswith('sky130_fd_sc_hd__dlclkp'): drv[pins['GCLK']] = (cell, pins, 'CLK')
    def trace(net):                        # gated clock net -> (root clk net, [icg gate nets]) or None
        seen = []
        while net in drv:
            cell, pins, inp = drv[net]
            if cell.startswith('sky130_fd_sc_hd__dlclkp'):
                return pins['GATE'], pins['CLK'], seen
            net = pins[inp]
        return None
    def root(net):
        while net in drv and BUF.match(drv[net][0]):
            net = drv[net][1]['A']
        return net
    out, last, n_icg, n_ff, n_other = [], 0, 0, 0, 0
    gclk_nets = {p['GCLK'] for _, c, _, p in insts if c.startswith('sky130_fd_sc_hd__dlclkp')}
    muxes = []
    for m, cell, name, pins in insts:
        repl = None
        if cell.startswith('sky130_fd_sc_hd__dlclkp'):
            repl = ''; n_icg += 1
        else:
            clk = pins.get('CLK')
            t = trace(clk) if clk and not cell.startswith('sky130_fd_sc_hd__dlclkp') and not BUF.match(cell) else None
            if t:
                en, icg_clk, _ = t
                if not FLOP_D.match(cell):
                    sys.exit('gated clock drives unsupported cell %s %s' % (cell, name))
                rclk = root(icg_clk)
                nd = '\\%s.ce_d ' % name.lstrip('\\').strip()
                muxes.append('  sky130_fd_sc_hd__mux2_1 \\%s.ce_mux  (.A0(%s), .A1(%s), .S(%s), .X(%s));\n'
                             % (name.lstrip('\\').strip(), esc(pins['Q']), esc(pins['D']), esc(en), nd))
                body = m.group(0)
                body = re.sub(r'\.CLK\(\s*[^)]*?\)', '.CLK(%s)' % esc(rclk), body, count=1)
                body = re.sub(r'\.D\(\s*[^)]*?\)', '.D(%s)' % nd, body, count=1)
                repl = body; n_ff += 1
        if repl is not None:
            out.append(text[last:m.start()]); out.append(repl); last = m.end()
    out.append(text[last:])
    res = ''.join(out)
    # declare the new wires and add the muxes before the final endmodule
    wires = ''.join('  wire \\%s.ce_d ;\n' % re.search(r'\\(\S+)\.ce_mux', x).group(1) for x in muxes)
    k = res.rindex('endmodule')
    res = res[:k] + wires + ''.join(muxes) + res[k:]
    open(dst, 'w').write(res)
    print('gl_icg_to_ce: %d clock gates removed, %d flops converted to clock enable' % (n_icg, n_ff))

main(*sys.argv[1:3])
