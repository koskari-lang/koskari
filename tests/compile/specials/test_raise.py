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
tests.compile.specials.test_raise -- Tests for the raise special form

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from koskari.exceptions import SyntaxMatchError, UncaughtCompilerError

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestRaiseSpecial(SpecialsTestCase):

    def test_two_arguments_fail_to_match(self):
        """
        (raise 1 2) is refused by the matcher at the form's position
        """

        with self.assertRaises(SyntaxMatchError) as cm:
            self.compile_expr("(raise 1 2)")

        exc = cm.exception
        self.assertNotIsInstance(exc, UncaughtCompilerError)
        self.assertIn("no matching syntax rule", str(exc))
        self.assertIn(
            "SyntaxMatchError: no matching syntax rule for compile/raise",
            exc.explain())
        self.assertEqual((exc.line, exc.column), (1, 0))


    def test_two_arguments_nested_position(self):
        """
        The position names the raise form, not the enclosing form
        """

        src = """
        (begin
          (raise 1 2))
        """

        with self.assertRaises(SyntaxMatchError) as cm:
            self.compile_expr(src.strip("\n"))

        exc = cm.exception
        self.assertEqual((exc.line, exc.column), (2, 2))


    async def test_bare_reraise_compiles(self):
        """
        (raise) with no argument re-raises the handled exception
        """

        src = """
        (try (int "x")
          [[ValueError] (raise)])
        """

        expr, _mod = self.compile_expr(
            src.strip("\n"), int=int, ValueError=ValueError)
        with self.assertRaises(ValueError):
            await expr()


    async def test_one_argument_raises_value(self):
        """
        (raise EXPR) raises the value of EXPR
        """

        expr, _mod = self.compile_expr(
            '(raise (KeyError "k"))', KeyError=KeyError)
        with self.assertRaises(KeyError):
            await expr()


# The end.
