"""Self-test for summary.py:  python3 local-ci/test_summary.py"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


class Summary(unittest.TestCase):
    def test_run(self):
        with tempfile.TemporaryDirectory() as d:
            j = os.path.join(d, "jobs")
            w(j + "/gds/status", "success\n")
            w(j + "/gds/started", "1000\n")
            w(j + "/gds/finished", "1300\n")
            w(j + "/gds/steps.tsv", "01\tBuild\tsuccess\t1000\t1300\n")
            w(j + "/gds/summary.md", "# Routing summary\n| Utilisation (%) | 41.5 |\n\n7 warnings\n")
            w(j + "/precheck/status", "failure\n")
            w(j + "/precheck/steps.tsv", "01\tInstall\tsuccess\t1300\t1310\n02\tRun\tfailure\t1310\t1345\n03\tX\tskipped\t\t\n")
            w(j + "/gl_test/status", "skipped\n")
            w(j + "/gl_test/steps.tsv", "")
            r = subprocess.run([sys.executable, os.path.join(HERE, "summary.py"), d], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            o = json.loads(r.stdout)
        self.assertEqual(o["schema"], 1)
        self.assertEqual(o["jobs"]["gds"], {"status": "success", "secs": 300})
        self.assertEqual(o["jobs"]["precheck"], {"status": "failure", "secs": 45})   # summed from steps.tsv
        self.assertEqual(o["jobs"]["gl_test"], {"status": "skipped", "secs": 0})
        self.assertEqual(o["failed"], ["precheck"])
        self.assertEqual((o["util"], o["warnings"]), (41.5, 7))

    def test_no_numbers_is_null(self):
        with tempfile.TemporaryDirectory() as d:
            w(d + "/jobs/gds/status", "running\n")
            o = json.loads(subprocess.run([sys.executable, os.path.join(HERE, "summary.py"), d],
                                          capture_output=True, text=True).stdout)
        self.assertEqual((o["util"], o["warnings"], o["failed"]), (None, None, []))


if __name__ == "__main__":
    unittest.main()
