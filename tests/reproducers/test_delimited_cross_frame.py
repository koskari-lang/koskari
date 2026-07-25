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
Delimited capture where the shift is not in the reset's own code object.

Chain order root..top is caller FRAME, PROMPT, callee FRAME, SHIFT, so a
cross-frame shift's owning frame sits ABOVE the prompt. Capture records
RESUME_PC and RESUME_STACK against that frame (kos_shift_capture takes the
first frame walking down from the captured chain's top), so the in-place
invoke has to resolve the same one. Resolving the frame at-or-below the
prompt instead installs the callee's pc and stack into the caller's frame,
which surfaced as "program counter out of bounds", silent control loss, or
SIGSEGV depending on shape.

Every pre-existing reset/shift test places the shift lexically inside the
reset body; the deeper-frame tests there invoke k from a deeper frame,
which is the opposite direction. Capture from a callee frame was
uncovered, which is why this went unnoticed.

The last two cases were quarantined in a separate uncollected module while
they SIGSEGV'd, since a crash takes the whole pytest process down rather
than failing one test. They pass now and are collected normally.

Note these shapes are order-sensitive under compile_expr: define-global
bindings do not persist across a fresh module per test, so running this
file alone yields extra NameErrors that the phased runner does not show.
The VM behaviour is the same either way; only the binding lookup differs.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


class TestDelimitedCrossFrameCapture(SpecialsTestCase):

    async def test_shift_is_entire_callee_body(self):
        """
        Prompt in the caller, shift is the callee's whole body
        """

        src = """
        (compile/begin
          (compile/define-global f (compile/lambda []
                              (compile/shift k (compile/call k 1))))
          (compile/reset (compile/call f)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1)


    async def test_pending_operand_in_caller_region(self):
        """
        Callee shifts; the caller's region has an operand pending, so the
        region's own block must survive the resume
        """

        src = """
        (compile/begin
          (compile/define-global f (compile/lambda []
                              (compile/shift k (compile/call k 5))))
          (compile/reset (+ 1 (compile/call f))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 6)


    async def test_pending_operand_in_callee_frame(self):
        """
        The callee has an operand pending around the shift, so the
        callee's own frame state must be captured and restored -- this is
        the case that cannot pass unless the correct frame is resumed
        """

        src = """
        (compile/begin
          (compile/define-global f (compile/lambda [n]
                              (+ n (compile/shift k (compile/call k 100)))))
          (compile/reset (+ 1 (compile/call f 2))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 103)


    async def test_shift_two_frames_deep(self):
        """
        Two frames between the shift and its prompt
        """

        src = """
        (compile/begin
          (compile/define-global inner (compile/lambda []
                                  (compile/shift k (compile/call k 1))))
          (compile/define-global outer (compile/lambda []
                                  (compile/call inner)))
          (compile/reset (compile/call outer)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1)


    async def test_cross_frame_abandonment(self):
        """
        A callee that shifts without invoking k abandons the region
        """

        src = """
        (compile/begin
          (compile/define-global f (compile/lambda []
                              (compile/shift k "abandoned")))
          (compile/reset (+ 1 (compile/call f))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), "abandoned")


    async def test_let_bound_lambda_shift_whole_body(self):
        """
        Let-bound lambda whose whole body is the shift. This shape
        SIGSEGV'd under the coroutine-driven test harness while the
        equivalent .ki program merely misbehaved.
        """

        src = """
        (compile/let [[f (compile/lambda []
                           (compile/shift k (compile/call k 1)))]]
          (compile/reset (compile/call f)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1)


    async def test_multishot_from_callee_collects_each_choice(self):
        """
        k invoked once per choice from a callee -- the shape a function
        form of amb needs.
        """

        src = """
        (compile/let [[collect
                       (compile/lambda [choices]
                         (compile/shift k
                           (compile/let [[out (compile/#list)]]
                             (compile/for-each [c choices]
                               (compile/call (compile/attr out append)
                                 (compile/call k c)))
                             out)))]]
          (compile/reset
            (+ 100 (compile/call collect (compile/#list 1 2 3)))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), [101, 102, 103])

# The end.
