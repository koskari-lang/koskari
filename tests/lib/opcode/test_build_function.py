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
Tests for BUILD_FUNCTION opcode.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from koskari.lib import FormalsSpec, Function
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestBuildFunction(CevalTest):
    """
    Tests for BUILD_FUNCTION: CONST (FunctionSpec), BUILD_FUNCTION N, RETURN 1.
    """

    async def test_build_function_no_defaults_returns_function(self):
        """
        CONST fspec, BUILD_FUNCTION 0, RETURN 1 yields a Function
        """

        inner_code = build_code([["RETURN", 0]], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code_obj, ns)()
        self.assertIsInstance(result, Function)


    async def test_build_function_returned_function_is_callable(self):
        """
        Function built via BUILD_FUNCTION is callable and returns a coroutine
        """

        inner_code = build_code([
            ["CONST", 99],
            ["RETURN", 1],
        ], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", 0],
            ["RETURN", 1],
        ])
        fn = await make_callable(code_obj, ns)()
        self.assertIsInstance(fn, Function)
        self.assertEqual(await fn(), 99)


    async def test_build_function_stack_underrun_raises(self):
        """
        BUILD_FUNCTION with no fspec on stack raises (stack underrun)
        """

        code_obj, ns = build_expr([
            ["BUILD_FUNCTION", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        with self.assertRaises(RuntimeError):
            await expr()


# The end.
