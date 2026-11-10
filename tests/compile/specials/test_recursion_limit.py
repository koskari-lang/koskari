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
Tests for the frame depth limit.

Koskari frames live on the heap, so a Koskari-to-Koskari call takes no
C stack and no Python frame object, and sys.getrecursionlimit() does not
count it. Frame depth is bounded by a process-wide limit, 10000 by
default and settable up to a cap, 65535 natively, both reported in
koskari.lib.build_flags, as a guard against runaway memory; exceeding
it raises RecursionError into the calling frame as an ordinary
exception. Where Koskari and Python call into each other, Python's own
recursion limit applies.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys

from koskari.lib import (
    build_flags, demand, get_frame_depth_limit, set_frame_depth_limit)

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


FRAME_DEPTH_MAX = build_flags["KOS_FRAME_DEPTH_MAX"]


DEEP = """
(begin
  (var f
    (lambda [n]
      (begin
        (call record n)
        (+ 1 (call f (+ n 1))))))
  (call f 1))
"""


DOWN = """
(begin
  (var down
    (lambda [n]
      (cond [(<= n 0) 0] [True (+ 1 (down (- n 1)))])))
  (down depth))
"""


TAIL = """
(begin
  (var loop
    (lambda [n acc]
      (cond [(<= n 0) acc] [True (loop (- n 1) (+ acc 1))])))
  (loop depth 0))
"""


MUTUAL = """
(begin
  (var mut nil)
  (setq mut
    (call demand
      (lambda [n]
        (begin
          (call record n)
          (cond
            [(<= n 0) 0]
            [True (+ 1 (call bounce mut (- n 1)))])))))
  (call mut depth))
"""


def bounce(f, n):
    """
    Call f from a Python frame object
    """

    return f(n)


class TestRecursionLimit(SpecialsTestCase):

    async def test_deep_recursion_raises(self):
        """
        non-tail recursion past the frame depth limit raises
        RecursionError naming the frame depth, and stops just short of
        the limit
        """

        limit = get_frame_depth_limit()
        seen = []
        expr, mod = self.compile_expr(DEEP, record=seen.append)
        with self.assertRaisesRegex(RecursionError, "frame depth"):
            await expr()
        self.assertLessEqual(seen[-1], limit)
        self.assertGreater(seen[-1], limit - 20)


    async def test_depth_past_python_limit(self):
        """
        a Koskari recursion deeper than the Python recursion limit, five
        times deeper where the frame depth cap allows, completes; the
        Python limit does not count Koskari frames
        """

        python_limit = sys.getrecursionlimit()
        depth = min(5 * python_limit, FRAME_DEPTH_MAX - 500)
        self.assertGreater(depth, python_limit)

        saved = get_frame_depth_limit()
        set_frame_depth_limit(max(saved, depth + 100))
        try:
            expr, mod = self.compile_expr(DOWN, depth=depth)
            self.assertEqual(await expr(), depth)
        finally:
            set_frame_depth_limit(saved)


    async def test_depth_just_under_limit(self):
        depth = get_frame_depth_limit() - 100
        expr, mod = self.compile_expr(DOWN, depth=depth)
        self.assertEqual(await expr(), depth)


    async def test_depth_past_limit_raises(self):
        depth = get_frame_depth_limit() + 100
        expr, mod = self.compile_expr(DOWN, depth=depth)
        with self.assertRaisesRegex(RecursionError, "frame depth"):
            await expr()


    async def test_depth_at_cap(self):
        """
        with the limit raised to the cap, a recursion just under the cap
        completes and one past it raises
        """

        saved = get_frame_depth_limit()
        set_frame_depth_limit(FRAME_DEPTH_MAX)
        try:
            expr, mod = self.compile_expr(DOWN, depth=FRAME_DEPTH_MAX - 500)
            self.assertEqual(await expr(), FRAME_DEPTH_MAX - 500)

            expr, mod = self.compile_expr(DOWN, depth=FRAME_DEPTH_MAX + 500)
            with self.assertRaisesRegex(RecursionError, "frame depth"):
                await expr()
        finally:
            set_frame_depth_limit(saved)


    async def test_tail_loop_adds_no_depth(self):
        expr, mod = self.compile_expr(TAIL, depth=1000000)
        self.assertEqual(await expr(), 1000000)


    async def test_limit_ignores_sys_setrecursionlimit(self):
        """
        a Python recursion limit lowered before evaluation starts does
        not bound Koskari frame depth
        """

        expr, mod = self.compile_expr(DOWN, depth=500)
        saved = sys.getrecursionlimit()
        sys.setrecursionlimit(200)
        try:
            result = await expr()
        finally:
            sys.setrecursionlimit(saved)
        self.assertEqual(result, 500)


    async def test_mutual_recursion_meets_python_limit(self):
        """
        a recursion that passes through a Python frame object on each
        round trip is bounded by Python's recursion limit, or by the
        eval nesting limit where that is lower, below the number of
        round trips Python's limit allows
        """

        limit = sys.getrecursionlimit()

        seen = []
        expr, mod = self.compile_expr(
            MUTUAL, depth=50, record=seen.append,
            demand=demand, bounce=bounce)
        self.assertEqual(await expr(), 50)

        seen = []
        expr, mod = self.compile_expr(
            MUTUAL, depth=limit, record=seen.append,
            demand=demand, bounce=bounce)
        with self.assertRaises(RecursionError):
            await expr()
        self.assertGreater(len(seen), 1)
        self.assertLess(len(seen), limit)


    async def test_recursion_error_is_catchable(self):
        """
        the error raises into the calling frame, so a try around the
        call handles it and evaluation continues
        """

        src = """
        (begin
          (var f
            (lambda [n]
              (+ 1 (call f (+ n 1)))))
          (try
            (call f 1)
            ([RecursionError] (call record "caught")))
          (call record "after"))
        """
        seen = []
        expr, mod = self.compile_expr(
            src, record=seen.append, RecursionError=RecursionError)
        await expr()
        self.assertEqual(seen, ["caught", "after"])


# The end.
