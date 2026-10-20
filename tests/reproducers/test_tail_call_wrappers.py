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
Tail calls through apply and partial from a frame that has a parent.

Both wrappers want no frame, so the tail prep pops the caller before
the install runs. The install used to forward the tail request into
the target's call, which prepped again against the caller's parent and
reset or popped a live frame: the parent's continuation was lost and
the result landed one frame too high.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import contextlib

import pytest

from koskari.lib import KosPartial as partial
from koskari.lib import apply
from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


def _add(a, b):
    return a + b


class TestTailCallThroughWrappers(SpecialsTestCase):

    async def _run(self, src, **values):
        # the bare specials harness has no builtins: the wrappers are
        # supplied here, and the loops below use cond, a special, since
        # if is a bootstrap macro and would compile as an eager call
        expr, mod = self.compile_expr(
            src, apply=apply, partial=partial, **values)
        return await expr()

    async def test_partial_over_koskari_function(self):
        src = """
        (begin
          (define [add a b] (+ a b))
          (var inc (partial add 1))
          (define [f x] (inc x))
          (define [h x] (* 10 (f x)))
          (h 5))
        """
        self.assertEqual(60, await self._run(src))


    async def test_partial_over_python_callable(self):
        src = """
        (begin
          (var inc (partial add 1))
          (define [f x] (inc x))
          (define [h x] (* 10 (f x)))
          (h 5))
        """
        self.assertEqual(60, await self._run(src, add=_add))


    async def test_apply_over_koskari_function(self):
        src = """
        (begin
          (define [add a b] (+ a b))
          (define [g x] (apply add #[x 1]))
          (define [h x] (* 10 (g x)))
          (h 5))
        """
        self.assertEqual(60, await self._run(src))


    async def test_apply_over_python_callable(self):
        src = """
        (begin
          (define [g x] (apply add #[x 1]))
          (define [h x] (* 10 (g x)))
          (h 5))
        """
        self.assertEqual(60, await self._run(src, add=_add))


    async def test_apply_from_a_caller_holding_a_with(self):
        # the caller's parent has a WITH extent open; the target must not
        # be tail-prepped against it
        src = """
        (begin
          (define [add a b] (+ a b))
          (define [g x] (apply add #[x 1]))
          (define [w x] (with [cm (nullcontext)] (g x)))
          (define [hw x] (* 10 (w x)))
          (hw 5))
        """
        self.assertEqual(
            60, await self._run(src, nullcontext=contextlib.nullcontext))


    async def test_deep_tail_recursion_through_apply_stays_flat(self):
        # the caller's frame is popped before the target's is pushed, so
        # depth does not grow with the iteration count
        src = """
        (begin
          (define [loop n acc]
              (cond [(== n 0) acc]
                    [else: (apply loop #[(- n 1) (+ acc 1)])]))
          (loop 20000 0))
        """
        self.assertEqual(20000, await self._run(src))


    async def test_deep_tail_recursion_through_partial_stays_flat(self):
        src = """
        (begin
          (define [step k n acc]
              (cond [(== n 0) acc]
                    [else: (bump (- n k) (+ acc k))]))
          (var bump (partial step 1))
          (bump 20000 0))
        """
        self.assertEqual(20000, await self._run(src))


# The end.
