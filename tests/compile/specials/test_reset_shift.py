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
Tests for koskari.compile compile/reset and compile/shift special forms.

reset establishes a prompt; shift captures the continuation and runs a
handler. Mirrors the semantics in design/language/control-flow.md.
"""

from . import SpecialsTestCase
from koskari.lib import ParamRef
from koskari.dis import dis

import pytest

pytestmark = pytest.mark.phase_specials


class TestResetShiftSpecials(SpecialsTestCase):

    async def test_reset_shift_invoke(self):
        """
        (compile/reset (+ 1 (compile/shift k (compile/call k 5)))) => 6
        """

        add_fn = lambda a, b: a + b
        src_str = """
        (compile/reset (+ 1 (compile/shift k (compile/call k 5))))
        """
        expr, mod = self.compile_expr(src_str, **{'+': add_fn})
        result = await expr()
        self.assertEqual(result, 6)


    async def test_reset_shift_return_cont(self):
        """
        (compile/reset (compile/shift k k)) => continuation
        """

        src_str = """
        (compile/reset (compile/shift k k))
        """
        expr, mod = self.compile_expr(src_str)
        cont = await expr()
        self.assertIsNotNone(cont)
        self.assertTrue(callable(cont))


    async def test_shift_stores_and_invokes(self):
        """
        reset returns k via (shift k k); store in outer scope, then invoke k(42)
        """

        src_str = """
        (compile/begin
          (compile/setq k (compile/reset (compile/shift k k)))
          (compile/call k 42))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_delimited_param_reset_shift_no_let(self):
        """
        parameterize + reset + shift without let. Continuation resumes add(10)
        = 10 + 100 = 110.
        """

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (compile/parameterize [[amt 100]]
          (compile/reset
            (add (compile/shift k (compile/call k 10)))))
        """

        expr, mod = self.compile_expr(src_str, add=add)

        result = await expr()
        self.assertEqual(result, 110)


    async def test_delimited_param_first_branch_only(self):
        """
        First branch only: parameterize amt=100, reset, shift stores k and
        invokes k(10). Continuation resumes add(10) = 10 + 100 = 110.
        """

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (compile/let [[stolen 0]]
          (compile/parameterize [[amt 100]]
            (compile/reset
              (add (compile/shift k (compile/setq stolen k) (compile/call k 10))))))
        """

        expr, mod = self.compile_expr(src_str, add=add)

        result = await expr()
        self.assertEqual(result, 110)


    async def test_delimited_let_reset_shift_no_param(self):
        """
        let + reset + shift without parameterize. Sanity check.
        """

        def add(a, b):
            print("add", a, b)
            return a + b

        src_str = """
        (compile/let [[stolen 0]]
          (compile/reset
            (add 1 (compile/shift k (compile/setq stolen k) (compile/call k 10)))))
        """

        expr, mod = self.compile_expr(src_str, add=add)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 11)


    async def test_delimited_param_outside_reset_not_captured(self):
        """
        Param outside reset is not captured; stolen k invoked in different
        param scope sees that scope's amt. Expected: (110, 209).

        First seq branch: parameterize amt=100, reset body runs. shift captures
        k, stores it in stolen, invokes k(10). Continuation resumes: add(10)
        = 10 + amt = 10 + 100 = 110.

        Second seq branch: parameterize amt=200, invoke stolen(9). Continuation
        resumes and transfers back to original param scope: add(9) = 9 + 100 = 109.
        """

        def seq(*args):
            return args

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (compile/parameterize [[amt 100]]
          (compile/let [[stolen 0]]
            (compile/call seq None
              (compile/call add 1)
              (compile/reset
                (compile/call add (compile/shift k (compile/setq stolen k) (compile/call k 10))))
              (compile/parameterize [[amt 200]]
                (compile/call stolen 9))
              (compile/call add 2)
            )))
        """

        expr, mod = self.compile_expr(src_str, add=add, seq=seq)
        # dis(expr.code)

        self.assertIs(mod.add, add)
        self.assertIs(mod.seq, seq)

        result = await expr()
        self.assertEqual(result, (None, 101, 110, 109, 102))


# The end.
