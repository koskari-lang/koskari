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
Tests for VAR, GLOBALS, LOCALS, CALL, TAIL_CALL, CALL_NAMES, TAIL_CALL_NAMES
opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import builtins

from koskari.lib import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestNamespaceAndCall(CevalTest):
    """
    Tests for VAR, GLOBALS, LOCALS, CALL, TAIL_CALL, CALL_NAMES, TAIL_CALL_NAMES
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


    async def test_globals_inside_push_ns_returns_module_namespace(self):
        """
        GLOBALS inside PUSH_NS returns module ns, not the inner let scope
        """

        child_nspec = NamespaceSpec("let", (), ())
        code, ns = build_expr([
            ["PUSH_NS", (child_nspec, None, 0, 0)],
            ["GLOBALS", 0],
            ["POP_NS", 1],
            ["RETURN", 1],
        ], values={"mod_marker": 99})
        result = await make_callable(code, ns)()
        self.assertIs(result, ns)
        self.assertEqual(result["mod_marker"], 99)


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
            ["BUILD_FUNCTION", ((), (), 0, 0)],
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
            ["BUILD_FUNCTION", ((), (), 0, 0)],
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


    async def test_tail_call_names_keyword_only_python_dict(self):
        """
        TAIL_CALL_NAMES opa=0: plain Python dict(...) with keywords only
        (no positionals). Regression for vectorcall args/count with kwnames.
        """

        code, ns = build_expr([
            ["CONST", builtins.dict],
            ["CONST", 1],
            ["CONST", ("foo",)],
            ["TAIL_CALL_NAMES", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, {"foo": 1})


    async def test_call_names_keyword_only_python_dict(self):
        """
        CALL_NAMES opa=0: same stack as TAIL_CALL_NAMES; non-tail path.
        """

        code, ns = build_expr([
            ["CONST", builtins.dict],
            ["CONST", 1],
            ["CONST", ("foo",)],
            ["CALL_NAMES", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, {"foo": 1})


    async def test_call_names_bound_kos_method_keyword_only(self):
        """
        CALL_NAMES with KosBoundMethod and keywords uses kos_vinstall_boundmethod.
        Regression: keyword values must be copied after prepended self (PEP 590 tail).
        """

        inner_code = build_code([
            ["VAR", "y"],
            ["RETURN", 1],
        ], name="inner")
        fspec = inner_code.function_spec(
            FormalsSpec(('self',), None, ('y',)))
        fn_code, fn_ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", ((), (), 0, 0)],
            ["RETURN", 1],
        ], name="make_fn")
        fn = await make_callable(fn_code, fn_ns)()
        bm = fn.__get__(object(), type(None))

        code, ns = build_expr([
            ["CONST", bm],
            ["CONST", 7],
            ["CONST", ("y",)],
            ["CALL_NAMES", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 7)


# The end.
