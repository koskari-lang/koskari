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
Tests for NEXT, ITER_NEXT opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestNextOpcodes(CevalTest):
    """
    Tests for NEXT (stack-based).
    """

    def test_next_1_arg_pushes_value(self):
        """
        NEXT 1 on iterator pushes first value
        """
        code, ns = build_expr([
            ["CONST", [10]],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 10)


    def test_next_1_arg_stopiteration_raises(self):
        """
        NEXT 1 on exhausted iterator raises StopIteration (or wraps it)
        """
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        coro = make_callable(code, ns)()
        with self.assertRaises((StopIteration, RuntimeError)):
            while True:
                coro.send(None)


    def test_next_2_arg_pushes_value(self):
        """
        NEXT 2 on iterator pushes value; stack is [iterator, default]
        """
        code, ns = build_expr([
            ["CONST", [7]],
            ["OBJECT_ITER", 0],
            ["CONST", 0],
            ["NEXT", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 7)


    def test_next_2_arg_exhausted_returns_default(self):
        """
        NEXT 2 on exhausted iterator pushes default
        """
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_ITER", 0],
            ["CONST", 42],
            ["NEXT", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_next_on_iterable_calls_get_iter(self):
        """
        NEXT on iterable (not iterator) calls PyObject_GetIter; yields first value
        """
        code, ns = build_expr([
            ["CONST", [100, 200]],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 100)


    def test_next_arg_validation_rejects_invalid_args(self):
        """
        NEXT with arg other than 1 or 2 raises ValueError
        """
        for bad_arg in (0, 3):
            code, ns = build_expr([
                ["CONST", []],
                ["OBJECT_ITER", 0],
                ["NEXT", bad_arg],
                ["RETURN", 1],
            ])
            with self.assertRaises(ValueError) as cm:
                run_to_completion(make_callable(code, ns)())
            self.assertIn("NEXT arg must be 1 or 2", str(cm.exception))


    def test_next_on_non_iterable_raises_type_error(self):
        """
        NEXT on object that is neither iterable nor iterator raises TypeError
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            run_to_completion(make_callable(code, ns)())

# The end.
