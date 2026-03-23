#!/usr/bin/env python3
"""Unit tests for compare_vcd.py"""

import subprocess
import sys
import tempfile
import os

SCRIPT = os.path.join(os.path.dirname(__file__), "compare_vcd.py")

VCD_A = """\
$timescale 1ps $end
$scope module top $end
$var wire 1 ! clk $end
$var wire 1 " q0 $end
$upscope $end
$enddefinitions $end
#0
0!
0"
#1
1!
1"
#2
0!
#3
1!
0"
#4
0!
"""

VCD_B_MATCH = """\
$timescale 1ps $end
$scope module top $end
$var wire 1 A sig_q0 $end
$upscope $end
$enddefinitions $end
#0
0A
#1
1A
#3
0A
"""

VCD_B_MISMATCH = """\
$timescale 1ps $end
$scope module top $end
$var wire 1 A sig_q0 $end
$upscope $end
$enddefinitions $end
#0
0A
#1
0A
"""


def run(vcd1, vcd2, mapping):
    with tempfile.NamedTemporaryFile(mode="w", suffix=".vcd", delete=False) as f1, \
         tempfile.NamedTemporaryFile(mode="w", suffix=".vcd", delete=False) as f2:
        f1.write(vcd1)
        f2.write(vcd2)
        f1.flush()
        f2.flush()
        try:
            r = subprocess.run(
                [sys.executable, SCRIPT, f1.name, f2.name, mapping],
                capture_output=True, text=True,
            )
            return r.returncode, r.stdout, r.stderr
        finally:
            os.unlink(f1.name)
            os.unlink(f2.name)


def test_match():
    rc, out, _ = run(VCD_A, VCD_B_MATCH, "q0=sig_q0")
    assert rc == 0, f"Expected match, got rc={rc}\n{out}"
    assert "OK" in out
    print("PASS: test_match")


def test_mismatch():
    rc, out, _ = run(VCD_A, VCD_B_MISMATCH, "q0=sig_q0")
    assert rc == 1, f"Expected mismatch, got rc={rc}\n{out}"
    assert "MISMATCH" in out
    print("PASS: test_mismatch")


if __name__ == "__main__":
    test_match()
    test_mismatch()
    print("All tests passed")
