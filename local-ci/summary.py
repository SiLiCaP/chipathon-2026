#!/usr/bin/env python3
"""Machine-readable summary of one local-ci run.

    summary.py <run-dir>          # .ci-local/runs/<id>

Prints one JSON object on stdout:
    {"schema": 1,
     "jobs": {"<job>": {"status": "success|failure|skipped|running|queued|cancelled|missing", "secs": <int>}},
     "failed": ["<job>", ...],
     "util": <float|null>,        # core utilisation (%) of the gds job, best effort
     "warnings": <int|null>}      # warning count from the gds job summary, best effort

Reads jobs/*/status and jobs/*/steps.tsv (columns written by pipeline.sh: NN, name, status, t0, t1 with
epoch seconds; skipped steps have empty t0/t1) and jobs/<job>/{started,finished} (epoch seconds) when
present. Stdlib only.
"""
import json
import os
import re
import sys


def read(path):
    try:
        with open(path, errors="replace") as f:
            return f.read().strip()
    except OSError:
        return None


def to_int(s):
    try:
        return int(float(s))
    except (TypeError, ValueError):
        return None


def job_secs(jdir):
    """Wall time of a job: finished - started if both exist, else the sum of its steps' t1 - t0."""
    s, f = to_int(read(os.path.join(jdir, "started"))), to_int(read(os.path.join(jdir, "finished")))
    if s is not None and f is not None and f >= s:
        return f - s
    total = 0
    for line in (read(os.path.join(jdir, "steps.tsv")) or "").splitlines():
        cols = line.split("\t")
        if len(cols) >= 5:
            a, b = to_int(cols[3]), to_int(cols[4])
            if a is not None and b is not None and b >= a:
                total += b - a
    return total


def parse_util(text):
    """Utilisation percentage from the routing/stats summary, e.g. '| Utilisation (%) | 41.2 |' or 'util: 41.2%'."""
    for pat in (r"(?i)utili[sz]ation[^0-9\n]{0,20}([0-9]+(?:\.[0-9]+)?)",
                r"(?i)\butil\b[^0-9\n]{0,10}([0-9]+(?:\.[0-9]+)?)"):
        m = re.search(pat, text)
        if m:
            return float(m.group(1))
    return None


def parse_warnings(text):
    m = re.search(r"(?i)(\d+)\s+warnings?\b", text)
    return int(m.group(1)) if m else None


def summarize(run_dir):
    jobs, failed = {}, []
    jroot = os.path.join(run_dir, "jobs")
    for name in sorted(os.listdir(jroot)) if os.path.isdir(jroot) else []:
        jdir = os.path.join(jroot, name)
        if not os.path.isdir(jdir):
            continue
        status = read(os.path.join(jdir, "status")) or "missing"
        jobs[name] = {"status": status, "secs": job_secs(jdir)}
        if status == "failure":
            failed.append(name)
    gds = read(os.path.join(jroot, "gds", "summary.md")) or ""
    return {"schema": 1, "jobs": jobs, "failed": failed,
            "util": parse_util(gds), "warnings": parse_warnings(gds)}


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    print(json.dumps(summarize(argv[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
