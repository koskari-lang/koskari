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
Tests for the native types after the koskari.lib module object has died

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import subprocess
import sys
import unittest

import pytest

from tests import needs_processes


pytestmark = pytest.mark.phase_lib


# The module object dies in interpreter shutdown, which only a process
# of its own can run to the end; the probe lives beside this module.
PROBE = os.path.join(os.path.dirname(__file__), "lib_shutdown_probe.py")


OPS = (
    "symbol", "fresh_symbol", "gensym", "pair_repr", "pair_str",
    "values_repr", "ref")


@needs_processes
@unittest.skipIf(
    sys.version_info < (3, 10),
    "3.9 runs no collection after clearing thread contexts, so the"
    " probe's finalizer never runs")
class TestTypesOutliveModule(unittest.TestCase):
    """
    The static types and their instances hold no reference to the
    module object, so they stay usable after it is collected
    """

    def run_op(self, op):
        return subprocess.run(
            [sys.executable, PROBE, op],
            capture_output=True, text=True)

    def test_finalizer_after_module_collection(self):
        for op in OPS:
            with self.subTest(op=op):
                proc = self.run_op(op)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertTrue(
                    proc.stderr.startswith("ok "), proc.stderr)


# The end.
