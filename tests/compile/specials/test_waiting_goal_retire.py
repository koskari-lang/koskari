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
tests.compile.specials.test_waiting_goal_retire

A delimited invoke made from inside a wind thunk that an earlier invoke
is entering leaves the earlier invoke waiting. Each landing settles
which waiting invokes are still live, so a run of nested invokes that
leaves one more waiting per pass must cost the same per pass however
many are waiting.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import time
import unittest

import pytest

from koskari.lib import (
    build_flags, get_frame_depth_limit, set_frame_depth_limit)

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


FRAME_DEPTH_MAX = build_flags["KOS_FRAME_DEPTH_MAX"]

# the longer run, in passes; each pass adds two frames
LONG_PASSES = 16000


NESTED_SRC = """
(let [[pass 0] [k nil]]
  (begin
    (setq k
      (reset
        (dyn-wind
          (begin
            (setq pass (+ pass 1))
            (tick pass)
            (cond
              ((> pass 1) (call k 99))
              (else: nil)))
          (shift k k)
          nil)))
    (call k 0)))
"""


class Bound(Exception):
    """
    Stops the nested invokes at the requested pass
    """


class TestWaitingGoalRetire(SpecialsTestCase):
    """
    Every pass re-enters the cloned wind's before-thunk, which invokes k
    again: the invoke entering that wind waits while the new one lands
    above it, so after p passes p - 1 invokes wait
    """

    async def best_time(self, expr, bound, passes):
        best = None
        for _ in range(5):
            bound[0] = passes
            start = time.perf_counter()
            with self.assertRaises(Bound):
                await expr()
            elapsed = time.perf_counter() - start
            best = elapsed if best is None else min(best, elapsed)
        return best

    @unittest.skipIf(
        FRAME_DEPTH_MAX < 2 * LONG_PASSES + 100,
        "the frame depth cap is below the passes the timing needs")
    async def test_cost_per_pass_does_not_grow_with_waiting_invokes(self):
        """
        Four times the passes takes about four times as long. A landing
        that visited every waiting invoke would take sixteen times as
        long, and one that walked the chain to each of them sixty-four
        times
        """

        bound = [0]

        def tick(n):
            if n >= bound[0]:
                raise Bound(n)

        expr, mod = self.compile_expr(NESTED_SRC, tick=tick)

        saved = get_frame_depth_limit()
        try:
            set_frame_depth_limit(FRAME_DEPTH_MAX)
            short = await self.best_time(expr, bound, LONG_PASSES // 4)
            long = await self.best_time(expr, bound, LONG_PASSES)
        finally:
            set_frame_depth_limit(saved)
        self.assertLess(long, short * 10, (short, long))


# The end.
