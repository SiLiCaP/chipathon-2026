#!/usr/bin/env python3
"""Fail unless the CI emulator contract (docs/ci-emulator.md) is intact. Stdlib only.

    python3 rp2350/ci/check_contract.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def read(rel):
    try:
        with open(os.path.join(ROOT, rel)) as f:
            return f.read()
    except OSError:
        return None


def main():
    bad = []
    app = read("rp2350/core/ci_app.c")
    if app is None or not re.search(r"\bint\s+app_ci\s*\(\s*ntt_chip_t\s*\*", app):
        bad.append("rp2350/core/ci_app.c must define int app_ci(ntt_chip_t *)")
    for rel in ("rp2350/rp/main.c", "rp2350/pc/pc_main.c"):
        s = read(rel)
        if s is None or not re.search(r'strcmp\(cmd,\s*"ci"\)', s) or "app_ci(" not in s:
            bad.append("%s must handle the `ci` command by calling app_ci" % rel)
    stub = read("rp2350/ci/ci_vectors.h")
    if stub is None or not re.search(r"#define\s+CI_CONTRACT\s+\d+", stub):
        bad.append("rp2350/ci/ci_vectors.h (the stub) must define CI_CONTRACT")
    if read("docs/ci-emulator.md") is None:
        bad.append("docs/ci-emulator.md is missing")
    for b in bad:
        print("check_contract: FAIL: " + b)
    if not bad:
        print("check_contract: ok (CI_CONTRACT in the stub, app_ci, `ci` in both mains, docs/ci-emulator.md)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
