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
Tests for PUSH_PARAM opcode

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from koskari.lib import Domain, ParamRef
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


class TestPushParamOpcodes(CevalTest):
    """
    Tests for PUSH_PARAM opcode (pop values, build param bindings, push extent)
    """

    async def test_push_param_single_binding(self):
        """
        PUSH_PARAM with one name pops one value and binds it
        """

        def check_x():
            return ParamRef("x").deref()

        code, ns = build_expr([
            ["CONST", 100],
            ["PUSH_PARAM", ("x",)],
            ["VAR", "f"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ], values={"f": check_x})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 100)


    async def test_push_param_two_bindings(self):
        """
        PUSH_PARAM with two names pops two values and binds both
        """

        def check_both():
            return (ParamRef("a").deref(), ParamRef("b").deref())

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["PUSH_PARAM", ("a", "b")],
            ["VAR", "f"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ], values={"f": check_both})
        result = await make_callable(code, ns)()
        self.assertEqual(result, (1, 2))


    async def test_push_param_nested_restores_on_pop_param(self):
        """
        Nested PUSH_PARAM: inner shadows outer; POP_PARAM restores outer
        """

        def read_z():
            return ParamRef("z").deref()

        code, ns = build_expr([
            ["CONST", 1],
            ["PUSH_PARAM", ("z",)],
            ["CONST", 2],
            ["PUSH_PARAM", ("z",)],
            ["VAR", "f"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["VAR", "f"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ], values={"f": read_z})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_push_param_unwind_on_return_restores_value(self):
        """
        RETURN inside PUSH_PARAM extent unwinds; param value is restored
        """

        p = ParamRef("z", None)
        code, ns = build_expr([
            ["CONST", 2],
            ["PUSH_PARAM", ("z",)],
            ["RETURN", 0],
            ["POP_PARAM", 0],
        ], values={"z": 1})
        with Domain():
            p.store(1)
            result = await make_callable(code, ns)()
            self.assertEqual(p.deref(), 1)
        self.assertIsNone(result)


    async def test_push_param_unwind_on_raise_restores_value(self):
        """
        Exception through PUSH_PARAM extent unwinds; param value is restored
        """

        p = ParamRef("z", None)

        def raiser():
            raise ValueError("expected")

        code, ns = build_expr([
            ["CONST", 2],
            ["PUSH_PARAM", ("z",)],
            ["VAR", "raiser"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["RETURN", 0],
        ], values={"raiser": raiser})
        with Domain():
            p.store(1)
            with self.assertRaises(ValueError):
                await make_callable(code, ns)()
            self.assertEqual(p.deref(), 1)


    async def test_push_param_stack_order_val0_deepest(self):
        """
        Stack: val for names[0] deepest, val for names[-1] at TOS
        """

        def check_order():
            return (ParamRef("first").deref(), ParamRef("second").deref())

        code, ns = build_expr([
            ["CONST", "deep"],
            ["CONST", "tos"],
            ["PUSH_PARAM", ("first", "second")],
            ["VAR", "f"],
            ["CALL", 0],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ], values={"f": check_order})
        result = await make_callable(code, ns)()
        self.assertEqual(result, ("deep", "tos"))


# The end.
