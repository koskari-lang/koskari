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
tests.compile.specials.test_tail_call_depth

A call in tail position reuses the frame. The proof is depth: a
self-recursion far past sys.getrecursionlimit() completes when the
recursive call is in tail position, and raises RecursionError when it
is not, for every call shape the compiler can put in tail position.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


# plain call, by name from the enclosing scope, with keyword names,
# through an attribute, and with a splat; the recursive call is the last
# thing the body does (cond, since if is a bootstrap macro this harness
# does not load)
TAIL_SHAPES = {
    "call": """
        (begin
          (var loop
            (lambda [n]
              (cond [(== n 0) "done"] [True (call loop (- n 1))])))
          (call loop depth))
    """,
    "var": """
        (begin
          (var loop
            (lambda [n]
              (cond [(== n 0) "done"] [True (loop (- n 1))])))
          (loop depth))
    """,
    "names": """
        (begin
          (var loop
            (lambda [n]
              (cond [(== n 0) "done"] [True (loop n: (- n 1))])))
          (loop n: depth))
    """,
    "attr": """
        (begin
          (var loop
            (lambda [n]
              (cond [(== n 0) "done"] [True (box.loop (- n 1))])))
          (setattr box "loop" loop)
          (box.loop depth))
    """,
    "star": """
        (begin
          (var loop
            (lambda [n]
              (cond [(== n 0) "done"] [True (loop *: #[(- n 1)])])))
          (loop *: #[depth]))
    """,
}

# the same recursion with the call wrapped in an operator, so it is no
# longer in tail position
NON_TAIL = """
    (begin
      (var loop
        (lambda [n]
          (cond [(== n 0) 0] [True (+ 1 (loop (- n 1)))])))
      (loop depth))
"""


class Box:
    pass


class TestTailCallDepth(SpecialsTestCase):

    def depth(self):
        return sys.getrecursionlimit() * 4

    async def test_tail_shapes_run_past_the_recursion_limit(self):
        for shape, src in TAIL_SHAPES.items():
            with self.subTest(shape=shape):
                expr, mod = self.compile_expr(
                    src, depth=self.depth(), box=Box(), setattr=setattr)
                self.assertEqual("done", await expr())

    async def test_non_tail_recursion_still_raises(self):
        expr, mod = self.compile_expr(NON_TAIL, depth=self.depth())
        with self.assertRaises(RecursionError):
            await expr()


# The end.
