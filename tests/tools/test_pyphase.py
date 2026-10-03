# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
Tests for tools.pyphase, the phased test runner

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import re
import subprocess
import sys
import tempfile
import unittest


TOPDIR = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


SAMPLE = '''
import pytest

pytestmark = pytest.mark.phase_lib


@pytest.mark.skipif(True, reason="always skipped at setup")
def test_skipped_at_setup():
    pass


@pytest.fixture
def broken_teardown():
    yield
    raise RuntimeError("teardown boom")


def test_call_passes_teardown_fails(broken_teardown):
    pass


def test_plain():
    pass
'''


def _total(summary, label):
    """
    The count printed for label in a PHASE SUMMARY, or None
    """

    found = re.search(rf"^{label}:\s+(\d+)\s*$", summary, re.MULTILINE)
    return int(found.group(1)) if found else None


class TestPhaseTotals(unittest.TestCase):

    def run_pyphase(self, source):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "test_sample.py")
            with open(path, "w") as out:
                out.write(source)
            proc = subprocess.run(
                [sys.executable, os.path.join(TOPDIR, "tools", "pyphase.py"),
                 "-p", "no:cacheprovider", "--rootdir", TOPDIR,
                 "-c", os.path.join(TOPDIR, "setup.cfg"), path],
                cwd=TOPDIR, capture_output=True, text=True)
        return proc.stdout, proc.stdout + proc.stderr


    def test_setup_skips_and_teardown_errors_are_counted(self):
        out, log = self.run_pyphase(SAMPLE)
        self.assertEqual(_total(out, "passed"), 2, log)
        self.assertEqual(_total(out, "failed"), 0, log)
        self.assertEqual(_total(out, "skipped"), 1, log)
        self.assertEqual(_total(out, "errors"), 1, log)


# The end.
