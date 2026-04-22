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

from tests.compile.specials import SpecialsTestCase

import pytest

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
        (compile/begin
          (compile/call mark "outer:before")
          (compile/reset
            (compile/begin
              (compile/call mark "inner:before")
              (compile/shift k
                (compile/call mark "inner:handler")
                (compile/call k 7))
              (compile/call mark "inner:after")))
          (compile/call mark "outer:after"))
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
        (compile/let [[outer 0]]
          (compile/begin
            (compile/setq outer
              (compile/reset
                (compile/shift k k)))
            (compile/call mark "before-inner")
            (compile/reset
              (compile/begin
                (compile/call mark "inner:start")
                (compile/shift k_inner
                  (compile/call mark "inner:handler")
                  (compile/call k_inner 21))
                (compile/call mark "inner:after")))
            (compile/call mark "before-outer-invoke")
            (compile/call mark (compile/call outer 99))
            (compile/call mark "after-outer-invoke")))
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
        (compile/reset
          (compile/begin
            (compile/call mark "outer:start")
            (compile/shift k_outer
              (compile/begin
                (compile/call mark "outer:handler:start")
                (compile/reset
                  (compile/begin
                    (compile/call mark "inner:start")
                    (compile/shift k_inner
                      (compile/call mark "inner:handler")
                      (compile/call k_inner 21))
                    (compile/call mark "inner:after")))
                (compile/call mark "outer:handler:before-k")
                (compile/call k_outer 99)))
            (compile/call mark "outer:after")))
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


    async def test_capture_store_and_invoke_across_prompt_boundary(self):
        events, mark = make_accumulator()

        src = """
        (compile/let [[stolen 0]]
          (compile/begin
            (compile/setq stolen
              (compile/reset
                (compile/shift k k)))
            (compile/call mark "before-invoke")
            (compile/call mark (compile/call stolen 42))))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, 42)
        self.assertEqual(events, ["before-invoke", 42])


    async def test_multi_invoke_captured_continuation_keeps_order(self):
        events, mark = make_accumulator()

        src = """
        (compile/let [[k 0]]
          (compile/begin
            (compile/setq k (compile/reset (compile/shift k k)))
            (compile/call mark (compile/call k 1))
            (compile/call mark (compile/call k 2))
            (compile/call mark "done")))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "done")
        self.assertEqual(events, [1, 2, "done"])


    async def test_two_captured_continuations_remain_independent(self):
        events, mark = make_accumulator()

        src = """
        (compile/let [[k1 0] [k2 0]]
          (compile/begin
            (compile/setq k1 (compile/reset (compile/shift k k)))
            (compile/setq k2 (compile/reset (compile/shift k k)))
            (compile/call mark (compile/call k1 1))
            (compile/call mark (compile/call k2 2))
            (compile/call mark "done")))
        """

        expr, _mod = self.compile_expr(src, mark=mark)
        result = await expr()

        self.assertEqual(result, "done")
        self.assertEqual(events, [1, 2, "done"])


# The end.
