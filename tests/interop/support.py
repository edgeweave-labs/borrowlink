"""Shared helpers for the interop tests.

The EdgeWeave Python side is imported from a directory given by EDGEWEAVE_DIR
(tests/interop/run.sh points it at a read-only snapshot of the EdgeWeave
repository, so the real checkout is never written to).  The C binaries come
from BL_NODE_BIN and BL_CROSSCHECK_BIN.
"""
import os
import subprocess
import sys

EDGEWEAVE_DIR = os.environ.get("EDGEWEAVE_DIR", "")
NODE_BIN = os.environ.get("BL_NODE_BIN", "")
CROSSCHECK_BIN = os.environ.get("BL_CROSSCHECK_BIN", "")
OUT_DIR = os.environ.get("BL_INTEROP_OUT", "/var/tmp")

if EDGEWEAVE_DIR and EDGEWEAVE_DIR not in sys.path:
    sys.path.insert(0, EDGEWEAVE_DIR)
sys.dont_write_bytecode = True


def require_env():
    missing = [n for n, v in (("EDGEWEAVE_DIR", EDGEWEAVE_DIR), ("BL_NODE_BIN", NODE_BIN),
                              ("BL_CROSSCHECK_BIN", CROSSCHECK_BIN)) if not v]
    if missing:
        raise RuntimeError("run tests/interop/run.sh (missing %s)" % ", ".join(missing))


class CTool:
    """A long-lived crosscheck_tool process: one line in, one line out."""

    def __init__(self):
        self.p = subprocess.Popen([CROSSCHECK_BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)

    def ask(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return self.p.stdout.readline().strip()

    def close(self):
        self.p.stdin.close()
        self.p.wait(timeout=5)
