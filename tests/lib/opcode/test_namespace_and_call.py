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
Tests for VAR, GLOBALS, LOCALS, CALL, TAIL_CALL opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from koskari.lib import FormalsSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


class TestNamespaceAndCall(CevalTest):
    """
    Tests for VAR, GLOBALS, LOCALS, CALL, TAIL_CALL
    """

    async def test_var_loads_from_namespace(self):
        """
        VAR name loads value from namespace onto stack
        """

        code, ns = build_expr([
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": 99})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_var_clear_raises_name_error_for_missing_name(self):
        """
        VAR_CLEAR raises NameError when name is not defined
        """

        code_obj, ns = build_expr([["VAR_CLEAR", "x"], ["RETURN", 1]])
        with self.assertRaises(NameError):
            await make_callable(code_obj, ns)()


    async def test_var_ref_raises_name_error_for_missing_name(self):
        """
        VAR_REF raises NameError when name is not defined
        """

        code_obj, ns = build_expr([["VAR_REF", "x"], ["RETURN", 1]])
        with self.assertRaises(NameError):
            await make_callable(code_obj, ns)()


    async def test_var_store_with_empty_stack_raises(self):
        """
        VAR_STORE with empty stack raises (stack underrun or RuntimeError)
        """

        code_obj, ns = build_expr([["VAR_STORE", "x"], ["RETURN", 1]])
        with self.assertRaises(RuntimeError):
            await make_callable(code_obj, ns)()


    async def test_globals_pushes_globals_dict(self):
        """
        GLOBALS pushes the frame globals (namespace) onto stack
        """

        code, ns = build_expr([
            ["GLOBALS", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, ns)


    async def test_locals_pushes_locals_dict(self):
        """
        LOCALS pushes the frame locals (namespace) onto stack
        """

        code, ns = build_expr([
            ["LOCALS", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, ns)


    async def test_call_invokes_callable_zero_args(self):
        """
        CALL 0 invokes callable on stack with no args
        """

        def forty_two():
            return 42

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": forty_two})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_call_invokes_callable_one_arg(self):
        """
        CALL 1 invokes callable with one argument (vectorcall path)
        """

        def double(x):
            return x * 2

        code, ns = build_expr([
            ["VAR", "f"],
            ["CONST", 21],
            ["CALL", 1],
            ["RETURN", 1],
        ], values={"f": double})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_tail_call_replaces_frame_returns_result(self):
        """
        TAIL_CALL with KosFunction replaces current frame; callee return value
        is the result of the invocation
        """

        inner_code = build_code([
            ["CONST", 42],
            ["RETURN", 1],
        ], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", 0],
            ["TAIL_CALL", 0],
        ])
        result = await make_callable(code_obj, ns)()
        self.assertEqual(result, 42)


    async def test_call_apply_helper_to_python_callable(self):
        """
        CALL apply delegates to python callable for normal objects
        """

        def plus(a, b, c=0):
            return a + b + c

        import koskari.lib as lib
        code, ns = build_expr([
            ["VAR", "apply"],
            ["VAR", "f"],
            ["CONST", (2, 3)],
            ["CONST", {"c": 4}],
            ["CALL", 3],
            ["RETURN", 1],
        ], values={"apply": lib.apply, "f": plus})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 9)


    async def test_call_apply_helper_to_koskari_function(self):
        """
        CALL apply with Koskari Function target executes and returns value
        """

        import koskari.lib as lib

        inner_code = build_code([
            ["CONST", 42],
            ["RETURN", 1],
        ], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        fn_code, fn_ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", 0],
            ["RETURN", 1],
        ], name="make_fn")
        fn = await make_callable(fn_code, fn_ns)()

        code, ns = build_expr([
            ["VAR", "apply"],
            ["VAR", "fn"],
            ["CALL", 1],
            ["RETURN", 1],
        ], values={"apply": lib.apply, "fn": fn})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


# The end.
