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
next is stepping arrives at the frame waiting on that next, and the
transfer clears the frame's resume while the context still names the
frame. The release of the generator runs a finalizer that asks for the
referents of every context; the process died by SIGSEGV when the frame
still reported the generator being deallocated.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


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
    """
    Released with the generator the transfer lets go of; its finalizer
    asks every context for its referents, which reports a frame's
    resume state through the kind's traverse
    """

    def __del__(self):
        for o in gc.get_objects():
            if type(o).__name__ == "Context":
                gc.get_referents(o)


class TestFrameResumeClearOrder(unittest.TestCase):

    def test_transfer_clears_next_resume_unnamed(self):
        """
        A transfer that clears a next's resume does not report the
        generator it releases
        """

        module = new_module("frame_resume_clear_order")
        asyncio.run(load_module_source_async(module, SRC))
        r = vars(module)["f"](Probe)
        if hasattr(r, "__await__"):
            r = asyncio.run(r)
        self.assertEqual("escaped", r)


# The end.
