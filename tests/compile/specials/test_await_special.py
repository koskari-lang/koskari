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
Tests for compile/await: run compiled expressions (see test_try.py pattern).
"""

from asyncio import sleep

import pytest

import koskari.lib as lib
from koskari.exceptions import SpecialError

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestCompileAwait(SpecialsTestCase):

    async def test_await_sleep_returns_value(self):
        src = """
            (begin
              (compile/await (sleep 0))
              42)
        """

        expr, _mod = self.compile_expr(
            src,
            sleep=sleep,
        )
        res = await expr()
        self.assertEqual(res, 42)


    async def test_await_defer_returns_value(self):
        src = """
            (compile/await (compile/call defer identity 42))
        """

        expr, _mod = self.compile_expr(
            src,
            defer=lib.defer,
            identity=lib.identity,
        )
        res = await expr()
        self.assertEqual(res, 42)


    async def test_await_bare_returns_none(self):
        src = """
            (compile/await)
        """
        expr, _mod = self.compile_expr(src)
        res = await expr()
        self.assertIsNone(res)


    async def test_await_bare(self):
        src = """
            (begin
              (compile/await)
              42)
        """
        expr, _mod = self.compile_expr(src)
        res = await expr()
        self.assertEqual(res, 42)


    def test_await_two_args_raises_at_compile(self):
        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/await a b)")


# The end.
