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
Wide tail calls through the install slot

A tail call that the call core cannot install itself goes through the
callee's install slot after the caller's frame is popped or reset.
When the argument window is wide enough to live in a heap segment
rather than an inline one, and the frame it sits on is freed rather
than parked, the storage the arguments were read from is gone before
the callee binds them, so the callee must be handed a vector that
outlives the frame. Each case here makes such a call with 20 arguments
from a function whose namespace a closure has captured, so its frame
cannot park in the function's cache. Correct results here are only
proof under ASan, where a read from the freed segment faults.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from koskari.lib import KosPartial as partial
from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


NUMS = " ".join(str(i) for i in range(20))


class TestTailCallWide(SpecialsTestCase):

    async def test_tail_call_partial_wide(self):
        """
        A tail call to a partial, which wants no frame of its own, so
        the caller's frame is popped before the partial forwards the
        arguments to its target
        """

        src = f"""
        (begin
          (define target (lambda [*: xs] xs))
          (define p (partial target))
          (define inner
            (lambda []
              (define keep (lambda [] 1))
              (p {NUMS})))
          (define outer (lambda [] (inner)))
          (outer))
        """

        expr, _mod = self.compile_expr(src, partial=partial)
        self.assertEqual(await expr(), tuple(range(20)))


    async def test_tail_call_partial_wide_from_nested_caller(self):
        """
        The same tail call reached from a frame that has a parent, so
        the pop the docstring above describes actually happens and the
        result must still come back to that parent
        """

        src = f"""
        (begin
          (define target (lambda [*: xs] xs))
          (define p (partial target))
          (define inner (lambda [] (p {NUMS})))
          (define outer (lambda [] (len (inner))))
          (outer))
        """
        expr, mod = self.compile_expr(src, partial=partial, len=len)
        self.assertEqual(20, await expr())


    async def test_tail_call_in_let_wide_positional(self):
        """
        A let-body tail call to a Koskari function: the window sits on
        the let's segment, which the tail install tears down
        """

        src = f"""
        (begin
          (define target (lambda [*: xs] xs))
          (define inner
            (lambda []
              (define keep (lambda [] 1))
              (let [[k 1]] (target {NUMS}))))
          (inner))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), tuple(range(20)))


    async def test_tail_call_in_let_wide_keywords(self):
        """
        The let-body case with keywords, which takes the general
        namespace build
        """

        src = f"""
        (begin
          (define target (lambda [*: xs **: kw] #(xs (sorted (kw.items)))))
          (define inner
            (lambda []
              (define keep (lambda [] 1))
              (let [[k 1]] (target {NUMS} a: 1 b: 2))))
          (inner))
        """

        expr, _mod = self.compile_expr(src, sorted=sorted)
        self.assertEqual(await expr(),
                         (tuple(range(20)), [("a", 1), ("b", 2)]))


# The end.
