# Imported from tt-viktor-chiptor @af81927 (host/tt_emu.py), maintained here with fpga/ (the PC side of the
# Tang Nano emulator protocol, fpga/PROTOCOL.md; used by tools/rig_pincheck.py).
"""PC driver for the Tang Nano 20K Tiny Tapeout emulator (protocol: fpga/PROTOCOL.md).

    python host/tt_emu.py --list            # show serial ports
    python host/tt_emu.py --ping            # auto-detect the board and ping it
    python host/tt_emu.py --ping --port /dev/ttyUSB1

The command encoders and response lengths are plain functions so the cocotb simulation of
the wrapper (fpga/sim) speaks exactly the same bytes as this driver.
"""
import argparse
import struct
import sys
import time

MAGIC = b"TTE1"
DEPTH = 8192          # stimulus / capture RAM words (fpga parameter AW = 13)
SYNC = 0xA5

OP_PING, OP_RESET, OP_SET_IN, OP_STEP, OP_SET_DIV = 0x00, 0x01, 0x02, 0x03, 0x04
OP_LOAD, OP_RUN, OP_READ_CAP = 0x10, 0x11, 0x12

# BL616 USB bridge on the Tang Nano 20K emulates an FT2232 ("SIPEED USB Debugger"): 0403:6010,
# measured on our board. Interface 0 is JTAG, interface 1 the UART; we still ping to confirm.
BOARD_USB_IDS = {(0x0403, 0x6010), (0x359F, 0x3101)}


# ---------- protocol encoding (shared with fpga/sim) ----------
def enc_ping():
    return bytes([OP_PING])


def enc_reset(cycles):
    return struct.pack("<BH", OP_RESET, cycles)


def enc_set_in(ui, uio):
    return struct.pack("<BBB", OP_SET_IN, ui & 0xFF, uio & 0xFF)


def enc_step(n):
    return struct.pack("<BH", OP_STEP, n)


def enc_set_div(d):
    return struct.pack("<BB", OP_SET_DIV, d)


def enc_load(addr, words):
    return struct.pack(f"<BHH{len(words)}H", OP_LOAD, addr, len(words), *words)


def enc_run(n):
    return struct.pack("<BH", OP_RUN, n)


def enc_read_cap(addr, n):
    return struct.pack("<BHH", OP_READ_CAP, addr, n)


def resp_len(op, n=0):
    """Total response bytes, including the A5 + status header."""
    return {OP_PING: 6, OP_STEP: 5, OP_READ_CAP: 2 + 3 * n}.get(op, 2)


def check_header(resp):
    if len(resp) < 2 or resp[0] != SYNC:
        raise IOError(f"bad response header: {resp[:8].hex()}")
    if resp[1] != 0:
        raise IOError(f"device returned status {resp[1]}")


def decode_caps(payload):
    """READ_CAP payload -> list of (uo_out, uio_out, uio_oe)."""
    return [tuple(payload[i:i + 3]) for i in range(0, len(payload), 3)]


def pin_word(ui, uio):
    """One stimulus word: {uio_in, ui_in}."""
    return ((uio & 0xFF) << 8) | (ui & 0xFF)


# ---------- serial driver ----------
class TTEmu:
    def __init__(self, port=None, baud=1000000, timeout=2.0):
        import serial
        self.port = port or find_port(baud)
        self.base_timeout = timeout
        self.phase_s = {"tx": 0.0, "calc": 0.0, "rx": 0.0}
        self.ser = serial.Serial(self.port, baud, timeout=timeout)
        self.resync()

    def close(self):
        self.ser.close()

    def resync(self):
        """Let the FPGA drop any half-received command (155 ms timeout), then flush."""
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def _xfer(self, cmd, op, n=0):
        want = resp_len(op, n)
        # pyserial's timeout covers the whole read, so allow for the bytes on the wire both ways
        self.ser.timeout = self.base_timeout + 1.5 * 10 * (len(cmd) + want) / self.ser.baudrate
        self.ser.write(cmd)
        resp = self.ser.read(want)
        if len(resp) != want:
            raise TimeoutError(f"op 0x{op:02x}: got {len(resp)}/{want} bytes")
        check_header(resp)
        return resp[2:]

    def ping(self):
        magic = self._xfer(enc_ping(), OP_PING)
        if magic != MAGIC:
            raise IOError(f"bad magic {magic!r}")
        return magic

    def reset(self, cycles=10):
        self._xfer(enc_reset(cycles), OP_RESET)

    def set_inputs(self, ui, uio):
        self._xfer(enc_set_in(ui, uio), OP_SET_IN)

    def step(self, n=1):
        return tuple(self._xfer(enc_step(n), OP_STEP))

    def set_div(self, d):
        self._xfer(enc_set_div(d), OP_SET_DIV)

    def load(self, addr, words):
        self._xfer(enc_load(addr, list(words)), OP_LOAD)

    def run(self, n):
        self._xfer(enc_run(n), OP_RUN)

    def read_cap(self, addr, n):
        return decode_caps(self._xfer(enc_read_cap(addr, n), OP_READ_CAP, n))

    def run_stream(self, words):
        """Play words at full DUT speed, chunked to RAM size; returns one capture per word.
        The DUT clock pauses between chunks with inputs held, which a synchronous design ignores."""
        words = list(words)
        out = []
        for i in range(0, len(words), DEPTH):
            chunk = words[i:i + DEPTH]
            t0 = time.perf_counter()
            self.load(0, chunk)
            t1 = time.perf_counter()
            self.run(len(chunk))
            t2 = time.perf_counter()
            out += self.read_cap(0, len(chunk))
            t3 = time.perf_counter()
            self.phase_s["tx"] += t1 - t0          # USB: stimulus words into the FPGA
            self.phase_s["calc"] += t2 - t1        # the chip clocking through them (13.5 MHz) + command round trip
            self.phase_s["rx"] += t3 - t2          # USB: captured result bytes back to the PC
        return out


def candidate_ports():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    board = [p for p in ports if (p.vid, p.pid) in BOARD_USB_IDS]
    # UART is the higher interface number, so try it first
    board.sort(key=lambda p: p.location or p.device, reverse=True)
    return board or [p for p in ports if p.vid is not None]


def find_port(baud=1000000):
    """The board shows up as two interfaces (JTAG + UART); ping each and keep the one that answers."""
    import serial
    tried = []
    for p in candidate_ports():
        tried.append(p.device)
        try:
            with serial.Serial(p.device, baud, timeout=0.3) as s:
                time.sleep(0.2)
                s.reset_input_buffer()
                s.write(enc_ping())
                if s.read(6) == bytes([SYNC, 0]) + MAGIC:
                    return p.device
        except (OSError, serial.SerialException):
            pass
    raise IOError(f"no emulator answered on {tried or 'any USB serial port'}; "
                  "is the bitstream loaded and are you in the dialout group?")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=1000000)
    ap.add_argument("--list", action="store_true", help="list serial ports")
    ap.add_argument("--ping", action="store_true", help="ping the emulator")
    a = ap.parse_args()
    if a.list:
        from serial.tools import list_ports
        for p in list_ports.comports():
            vid = f"{p.vid:04x}:{p.pid:04x}" if p.vid is not None else "-"
            print(f"{p.device:16} {vid:10} {p.description}")
    if a.ping:
        emu = TTEmu(a.port, a.baud)
        print(f"{emu.port}: magic {emu.ping().decode()} OK")
    if not (a.list or a.ping):
        ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
