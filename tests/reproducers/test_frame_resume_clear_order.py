# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
koskari.tests.reproducers.test_frame_resume_clear_order

A frame's resume is cleared with the frame no longer naming the state
being released. A continuation invoked from inside a generator that a
next is stepping lands on the frame waiting on that next, and the
landing clears the frame's resume while the context still names the
frame. The release of the generator runs a finalizer that asks for the
referents of every context; the process died by SIGSEGV when the frame
still reported the generator being deallocated.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import subprocess
import sys
import unittest

import pytest


pytestmark = pytest.mark.phase_reproducers


SCRIPT = '''
import asyncio
import gc

from koskari.module import load_module_source_async, new_module

SRC = """
(def make-mk [probe]
  (lambda [k] (begin probe (yield (call k "escaped")))))

(def f [newprobe]
  (let [[box (list)]]
    (let [[r (let/cc k (begin (box.append k) "first"))]]
      (if (== r "first")
          (next ((make-mk (newprobe)) (box.__getitem__ 0)))
          r))))
"""


class Probe:
    def __del__(self):
        for o in gc.get_objects():
            if type(o).__name__ == "Context":
                gc.get_referents(o)


module = new_module("frame_resume_clear_order")
asyncio.run(load_module_source_async(module, SRC))
r = vars(module)["f"](Probe)
if hasattr(r, "__await__"):
    r = asyncio.run(r)
print(r)
'''


class TestFrameResumeClearOrder(unittest.TestCase):

    def test_landing_clears_next_resume_unnamed(self):
        """
        A landing that clears a next's resume does not report the
        generator it releases
        """

        res = subprocess.run(
            [sys.executable, "-c", SCRIPT],
            capture_output=True, text=True, timeout=300)
        self.assertEqual(0, res.returncode, res.stderr)
        self.assertEqual("escaped\n", res.stdout)


# The end.
