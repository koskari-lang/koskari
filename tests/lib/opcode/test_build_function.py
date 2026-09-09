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

import inspect

import pytest

from koskari.lib import FormalsSpec, Function
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


pytestmark = pytest.mark.phase_lib


class TestBuildFunction(CevalTest):
    """
    Tests for BUILD_FUNCTION: CONST (FunctionSpec), BUILD_FUNCTION
    (defnames, annnames, 0, 0), RETURN 1.
    """

    async def test_build_function_no_defaults_returns_function(self):
        """
        CONST fspec, BUILD_FUNCTION ((), (), 0, 0), RETURN 1 yields a
        Function
        """

        inner_code = build_code([["RETURN", 0]], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", ((), (), 0, 0)],
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
            ["BUILD_FUNCTION", ((), (), 0, 0)],
            ["RETURN", 1],
        ])
        fn = await make_callable(code_obj, ns)()
        self.assertIsInstance(fn, Function)
        self.assertEqual(await fn(), 99)


    async def test_build_function_defaults_from_names_tuple(self):
        """
        A non-empty defaults names tuple pops that many stack values into
        the keyword defaults dict
        """

        inner_code = build_code([["RETURN", 0]], name="inner",
                                local_syms=("a",))
        fspec = inner_code.function_spec(FormalsSpec((), (), ("a",)))
        code_obj, ns = build_expr([
            ["CONST", 7],
            ["CONST", fspec],
            ["BUILD_FUNCTION", (("a",), (), 0, 0)],
            ["RETURN", 1],
        ])
        fn = await make_callable(code_obj, ns)()
        self.assertIsInstance(fn, Function)
        self.assertEqual(fn.formals.defaults, {"a": 7})


    async def test_build_function_annotations_from_names_tuple(self):
        """
        A non-empty annotation names tuple pops that many stack values
        into the creation-time annotations dict
        """

        inner_code = build_code([["RETURN", 0]], name="inner",
                                local_syms=("a",))
        fspec = inner_code.function_spec(FormalsSpec(("a",)))
        code_obj, ns = build_expr([
            ["CONST", int],
            ["CONST", int],
            ["CONST", fspec],
            ["BUILD_FUNCTION", ((), ("a", "return"), 0, 0)],
            ["RETURN", 1],
        ])
        fn = await make_callable(code_obj, ns)()
        self.assertIsInstance(fn, Function)
        self.assertEqual(fn.__annotations__, {"a": int, "return": int})


    async def test_build_function_defaults_and_annotations(self):
        """
        Annotation values sit below default values on the stack; both
        dicts are built from their respective names tuples
        """

        inner_code = build_code([["RETURN", 0]], name="inner",
                                local_syms=("a",))
        fspec = inner_code.function_spec(FormalsSpec((), (), ("a",)))
        code_obj, ns = build_expr([
            ["CONST", int],
            ["CONST", 7],
            ["CONST", fspec],
            ["BUILD_FUNCTION", (("a",), ("a",), 0, 0)],
            ["RETURN", 1],
        ])
        fn = await make_callable(code_obj, ns)()
        self.assertIsInstance(fn, Function)
        self.assertEqual(fn.formals.defaults, {"a": 7})
        self.assertEqual(fn.__annotations__, {"a": int})


    async def test_build_function_signature_reflects_both_dicts(self):
        """
        inspect.signature over the built function reports the keyword
        default and the annotation together
        """

        inner_code = build_code([["RETURN", 0]], name="inner",
                                local_syms=("a",))
        fspec = inner_code.function_spec(FormalsSpec((), (), ("a",)))
        code_obj, ns = build_expr([
            ["CONST", int],
            ["CONST", int],
            ["CONST", 7],
            ["CONST", fspec],
            ["BUILD_FUNCTION", (("a",), ("a", "return"), 0, 0)],
            ["RETURN", 1],
        ])
        fn = await make_callable(code_obj, ns)()
        sig = inspect.signature(fn)
        param = sig.parameters["a"]
        self.assertIs(param.annotation, int)
        self.assertEqual(param.default, 7)
        self.assertIs(sig.return_annotation, int)


    async def test_build_function_stack_underrun_raises(self):
        """
        BUILD_FUNCTION with no fspec on stack raises (stack underrun)
        """

        code_obj, ns = build_expr([
            ["BUILD_FUNCTION", ((), (), 0, 0)],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        with self.assertRaises(RuntimeError):
            await expr()


    async def test_build_function_annotation_underrun_raises(self):
        """
        BUILD_FUNCTION with an annotation names tuple but too few stack
        values raises (stack underrun)
        """

        inner_code = build_code([["RETURN", 0]], name="inner",
                                local_syms=("a",))
        fspec = inner_code.function_spec(FormalsSpec(("a",)))
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", ((), ("a",), 0, 0)],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        with self.assertRaises(RuntimeError):
            await expr()


# The end.
