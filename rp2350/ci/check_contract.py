#!/usr/bin/env python3
"""Fail unless the CI emulator contract (docs/ci-emulator.md) is intact. Stdlib only.

    python3 rp2350/ci/check_contract.py

Guards the plumbing the host CI relies on: the firmware's `ci` command and app_ci, the contract-2 stub header, the
software model, the committed golden vectors, and the FPGA emulator files (fpga/, tools/rig_pincheck.py) that
rp2350/ and the gate netlist are run on. Both directories are required parts of every push.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONTRACT = 2
REQUIRED = [
    "docs/ci-emulator.md",
    "fpga/Makefile", "fpga/tangnano20k_pico_top.v", "fpga/tangnano20k_pico.cst", "fpga/rig_pinchk.v",
    "fpga/pico_rig_pins.py", "fpga/PROTOCOL.md", "tools/rig_pincheck.py",
    "rp2350/CMakeLists.txt", "rp2350/pc/Makefile", "rp2350/core/hash_chip.c", "rp2350/core/hash_chip.h",
    "rp2350/core/hash_swchip.c", "rp2350/core/sha3_sw.c", "rp2350/ci/golden/ci_vectors.h",
]


def read(rel):
    try:
        with open(os.path.join(ROOT, rel)) as f:
            return f.read()
    except OSError:
        return None


def main():
    bad = []
    for rel in REQUIRED:
        if read(rel) is None:
            bad.append("%s is missing (fpga/ and rp2350/ are required parts of the repo)" % rel)
    app = read("rp2350/core/ci_app.c")
    if app is None or not re.search(r"\bint\s+app_ci\s*\(\s*hash_chip_t\s*\*", app):
        bad.append("rp2350/core/ci_app.c must define int app_ci(hash_chip_t *)")
    for rel in ("rp2350/rp/main.c", "rp2350/pc/pc_main.c"):
        s = read(rel)
        if s is None or not re.search(r'strcmp\(cmd,\s*"ci"\)', s) or "app_ci(" not in s:
            bad.append("%s must handle the `ci` command by calling app_ci" % rel)
    for rel in ("rp2350/ci/ci_vectors.h", "rp2350/ci/golden/ci_vectors.h"):
        s = read(rel)
        m = re.search(r"#define\s+CI_CONTRACT\s+(\d+)", s or "")
        if not m or int(m.group(1)) != CONTRACT:
            bad.append("%s must define CI_CONTRACT %d" % (rel, CONTRACT))
        elif "CI_NJOB" not in s or "ci_job" not in s or "ci_data" not in s:
            bad.append("%s lacks CI_NJOB / ci_job / ci_data" % rel)
    stub = read("rp2350/ci/ci_vectors.h") or ""
    if not re.search(r"#define\s+CI_NJOB\s+0\b", stub):
        bad.append("the stub rp2350/ci/ci_vectors.h must have CI_NJOB 0 (the firmware then prints ci: NOVECTORS)")
    mk = read("rp2350/pc/Makefile") or ""
    if "ci-golden" not in mk or "CI_INC" not in mk:
        bad.append("rp2350/pc/Makefile needs the ci-golden target and CI_INC")
    cm = read("rp2350/CMakeLists.txt") or ""
    if "CI_VECTORS_DIR" not in cm:
        bad.append("rp2350/CMakeLists.txt must take -DCI_VECTORS_DIR")
    for b in bad:
        print("check_contract: FAIL: " + b)
    if not bad:
        print("check_contract: ok (CI_CONTRACT %d stub + golden, app_ci, `ci` in both mains, fpga/, tools/, docs/ci-emulator.md)" % CONTRACT)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
