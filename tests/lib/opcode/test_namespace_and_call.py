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
Tests for VAR, GLOBALS, LOCALS, CALL opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestNamespaceAndCall(CevalTest):
    """
    Tests for VAR, GLOBALS, LOCALS, CALL
    """

    def test_var_loads_from_namespace(self):
        """
        VAR name loads value from namespace onto stack
        """

        code, ns = build_expr([
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": 99})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    def test_var_clear_raises_name_error_for_missing_name(self):
        """
        VAR_CLEAR raises NameError when name is not defined
        """

        code_obj, ns = build_expr([["VAR_CLEAR", "x"], ["RETURN", 1]])
        with self.assertRaises(NameError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_var_ref_raises_name_error_for_missing_name(self):
        """
        VAR_REF raises NameError when name is not defined
        """

        code_obj, ns = build_expr([["VAR_REF", "x"], ["RETURN", 1]])
        with self.assertRaises(NameError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_var_store_with_empty_stack_raises(self):
        """
        VAR_STORE with empty stack raises (stack underrun or RuntimeError)
        """

        code_obj, ns = build_expr([["VAR_STORE", "x"], ["RETURN", 1]])
        with self.assertRaises(RuntimeError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_globals_pushes_globals_dict(self):
        """
        GLOBALS pushes the frame globals (namespace) onto stack
        """

        code, ns = build_expr([
            ["GLOBALS", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIs(result, ns)


    def test_locals_pushes_locals_dict(self):
        """
        LOCALS pushes the frame locals (namespace) onto stack
        """

        code, ns = build_expr([
            ["LOCALS", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIs(result, ns)


    def test_call_invokes_callable_zero_args(self):
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
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)

    def test_call_invokes_callable_one_arg(self):
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
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)

# The end.
