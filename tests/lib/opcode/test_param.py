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
Tests for PARAM opcode

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import ParamRef
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestParamOpcodes(CevalTest):
    """
    Tests for PARAM opcode (load ParamRef by name from constant pool)
    """

    async def test_param_pushes_paramref_for_key(self):
        """
        PARAM pushes ParamRef for the given key onto the stack
        """

        code, ns = build_expr([
            ["PARAM", "x"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, ParamRef)
        self.assertEqual(result.name, "x")


    async def test_param_with_different_key(self):
        """
        PARAM with different key produces ParamRef for that key
        """

        code, ns = build_expr([
            ["PARAM", "foo"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, ParamRef)
        self.assertEqual(result.name, "foo")


    async def test_param_deref_returns_domain_value_when_set(self):
        """
        ParamRef from PARAM derefs to domain value when set via PUSH_PARAM
        """

        def check_x():
            return ParamRef("x").deref()

        code, ns = build_expr([
            ["CONST", 42],
            ["PUSH_PARAM", ("x",)],
            ["VAR", "f"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ], values={"f": check_x})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


# The end.
