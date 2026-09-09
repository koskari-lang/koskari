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
Reproducer coverage for nested reset/shift behavior.

These tests intentionally validate ordering, not just final values, to catch
misrouting in nested delimited-control flows.
"""

import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


def make_accumulator():
    events = []

    def mark(x):
        events.append(x)
        return x

    return events, mark


class TestNestedResetShiftReproducers(SpecialsTestCase):

    async def test_nested_shift_uses_nearest_prompt_only(self):
        events, mark = make_accumulator()

        src = """
        (begin
          (call mark "outer:before")
          (reset
            (begin
              (call mark "inner:before")
              (shift k
                (call mark "inner:handler")
                (call k 7))
              (call mark "inner:after")))
          (call mark "outer:after"))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "outer:after")
        self.assertEqual(events, [
            "outer:before",
            "inner:before",
            "inner:handler",
            "inner:after",
            "outer:after",
        ])


    async def test_outer_flow_survives_inner_delimited_invoke(self):
        events, mark = make_accumulator()

        src = """
        (let [[outer 0]]
          (begin
            (setq outer
              (reset
                (shift k k)))
            (call mark "before-inner")
            (reset
              (begin
                (call mark "inner:start")
                (shift k_inner
                  (call mark "inner:handler")
                  (call k_inner 21))
                (call mark "inner:after")))
            (call mark "before-outer-invoke")
            (call mark (call outer 99))
            (call mark "after-outer-invoke")))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "after-outer-invoke")
        self.assertEqual(events, [
            "before-inner",
            "inner:start",
            "inner:handler",
            "inner:after",
            "before-outer-invoke",
            99,
            "after-outer-invoke",
        ])


    async def test_root_block_nested_outer_invoke_survives_inner_reset(self):
        events, mark = make_accumulator()

        src = """
        (reset
          (begin
            (call mark "outer:start")
            (shift k_outer
              (begin
                (call mark "outer:handler:start")
                (reset
                  (begin
                    (call mark "inner:start")
                    (shift k_inner
                      (call mark "inner:handler")
                      (call k_inner 21))
                    (call mark "inner:after")))
                (call mark "outer:handler:before-k")
                (call k_outer 99)))
            (call mark "outer:after")))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "outer:after")
        self.assertEqual(events, [
            "outer:start",
            "outer:handler:start",
            "inner:start",
            "inner:handler",
            "inner:after",
            "outer:handler:before-k",
            "outer:after",
        ])


    async def test_minimal_nested_reset_shift_outer_invoke(self):
        src = """
        (reset
          (begin
            (shift k_outer
              (begin
                (reset
                  (shift k_inner
                    (call k_inner 1)))
                (call k_outer 2)))
            3))
        """

        expr, _mod = self.compile_expr(src)
        result = await expr()

        self.assertEqual(result, 3)


    async def test_capture_store_and_invoke_across_prompt_boundary(self):
        events, mark = make_accumulator()

        src = """
        (let [[stolen 0]]
          (begin
            (setq stolen
              (reset
                (shift k k)))
            (call mark "before-invoke")
            (call mark (call stolen 42))))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, 42)
        self.assertEqual(events, ["before-invoke", 42])


    async def test_multi_invoke_captured_continuation_keeps_order(self):
        events, mark = make_accumulator()

        src = """
        (let [[k 0]]
          (begin
            (setq k (reset (shift k k)))
            (call mark (call k 1))
            (call mark (call k 2))
            (call mark "done")))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "done")
        self.assertEqual(events, [1, 2, "done"])


    async def test_two_captured_continuations_remain_independent(self):
        events, mark = make_accumulator()

        src = """
        (let [[k1 0] [k2 0]]
          (begin
            (setq k1 (reset (shift k k)))
            (setq k2 (reset (shift k k)))
            (call mark (call k1 1))
            (call mark (call k2 2))
            (call mark "done")))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "done")
        self.assertEqual(events, [1, 2, "done"])


    async def test_shift_multicall_accumulates_even_series(self):
        events, mark = make_accumulator()

        def mul2(x):
            return x * 2

        def sum6(a, b, c, d, e, f):
            return a + b + c + d + e + f

        src = """
        (reset
          (call mark
            (call mul2
              (shift k
                (call sum6
                  (call k 0)
                  (call k 1)
                  (call k 2)
                  (call k 3)
                  (call k 4)
                  (call k 5))))))
        """

        expr, _mod = self.compile_expr(src, mark=mark, mul2=mul2, sum6=sum6)
        result = await expr()

        self.assertEqual(result, 30)
        self.assertEqual(events, [0, 2, 4, 6, 8, 10])


    async def test_nested_inner_multicall_continuation_invokes(self):
        events, mark = make_accumulator()

        def mul2(x):
            return x * 2

        def sum6(a, b, c, d, e, f):
            return a + b + c + d + e + f

        src = """
        (reset
          (begin
            (call mark "outer:start")
            (shift k_outer
              (begin
                (call mark "outer:handler:start")
                (reset
                  (call mark
                    (call mul2
                      (shift k_inner
                        (call sum6
                          (call k_inner 0)
                          (call k_inner 1)
                          (call k_inner 2)
                          (call k_inner 3)
                          (call k_inner 4)
                          (call k_inner 5))))))
                (call mark "outer:handler:before-k")
                (call k_outer 99)))
            (call mark "outer:after")))
        """

        expr, _mod = self.compile_expr(src, mark=mark, mul2=mul2, sum6=sum6)
        result = await expr()

        self.assertEqual(result, "outer:after")
        self.assertEqual(events, [
            "outer:start",
            "outer:handler:start",
            0,
            2,
            4,
            6,
            8,
            10,
            "outer:handler:before-k",
            "outer:after",
        ])


# The end.
