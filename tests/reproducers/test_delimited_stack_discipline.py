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
Operand-stack discipline across a delimited resume.

A PROMPT is a barrier only -- the operand stack stays on the frame -- so
the region's own operands are distinguished from an enclosing pending
expression's operands solely by the depth recorded at PUSH_PROMPT.

The in-place invoke path stashes the whole invoke-time frame stack and
restitch restores it, so without truncating to that depth the region's
pending operands survive underneath the resumed result. Every case here
therefore puts the reset inside an enclosing pending call AND leaves
operands pending inside the region; that combination is what displaces
the enclosing argument list. With the reset at a clean stack base the
residue sits below where nothing looks, which is why this went unnoticed.

Failures are silent wrong answers rather than errors, except where a
typed consumer (BUILD_LIST_APPEND) rejects the displaced operand.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


def joiner(*args):
    """
    Render an argument list so displacement is visible in the value
    """

    return "|".join(str(a) for a in args)


class TestDelimitedResumeStackDiscipline(SpecialsTestCase):

    async def test_no_operands_pending_in_region(self):
        """
        Control: enclosing operand, region has nothing pending
        """

        src = """
        (compile/call joiner "A"
          (compile/reset (compile/shift k (compile/call k 5))))
        """

        expr, _mod = self.compile_expr(src, joiner=joiner)
        self.assertEqual(await expr(), "A|5")


    async def test_one_enclosing_one_pending(self):
        """
        One enclosing operand, one operand pending in the region
        """

        src = """
        (compile/call joiner "A"
          (compile/reset (+ 1 (compile/shift k (compile/call k 5)))))
        """

        expr, _mod = self.compile_expr(src, joiner=joiner)
        self.assertEqual(await expr(), "A|6")


    async def test_two_enclosing_two_pending(self):
        """
        Two enclosing operands, two pending in the region
        """

        src = """
        (compile/call joiner "C" "D"
          (compile/reset (+ 1 2 (compile/shift k (compile/call k 5)))))
        """

        expr, _mod = self.compile_expr(src, joiner=joiner)
        self.assertEqual(await expr(), "C|D|8")


    async def test_three_pending_reduces_to_one_residue(self):
        """
        A reducing operator folds pending operands into one accumulator
        """

        src = """
        (compile/call joiner "B"
          (compile/reset (+ 1 2 3 (compile/shift k (compile/call k 4)))))
        """

        expr, _mod = self.compile_expr(src, joiner=joiner)
        self.assertEqual(await expr(), "B|10")


    async def test_pending_operand_is_a_call_argument(self):
        """
        Residue is not specific to reducing operators; a pending call
        argument leaks the same way
        """

        src = """
        (compile/call joiner "B"
          (compile/reset
            (compile/call mx 1 (compile/shift k (compile/call k 5)))))
        """

        expr, _mod = self.compile_expr(src, joiner=joiner, mx=max)
        self.assertEqual(await expr(), "B|5")


    async def test_reset_inside_pending_list_build(self):
        """
        A displaced operand reaching BUILD_LIST_APPEND is type-fatal
        rather than silently wrong
        """

        src = """
        (compile/#list "E"
          (compile/reset (+ 1 (compile/shift k (compile/call k 5)))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), ["E", 6])


    async def test_multishot_inside_pending_call(self):
        """
        Multi-shot resume must not accumulate residue per invoke
        """

        src = """
        (compile/call joiner "A"
          (compile/reset
            (+ 1 (compile/shift k
                   (+ (compile/call k 10) (compile/call k 20))))))
        """

        expr, _mod = self.compile_expr(src, joiner=joiner)
        self.assertEqual(await expr(), "A|32")


    async def test_nested_region_builds_at_clean_base(self):
        """
        Pending operands wholly inside the region stay correct
        """

        src = """
        (compile/#list "x"
          (+ 1 (compile/shift k (compile/call k 5)))
          "y")
        """

        src = f"(compile/reset {src})"
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), ["x", 6, "y"])

# The end.
