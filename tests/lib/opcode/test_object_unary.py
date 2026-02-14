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
Tests for OBJECT_STR, OBJECT_LEN opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestObjectUnaryOpcodes(CevalTest):
    """
    Tests for OBJECT_STR, OBJECT_LEN, OBJECT_NOT, OBJECT_TRUTH,
    OBJECT_TYPE, OBJECT_VARS, OBJECT_AITER, OBJECT_REPR, OBJECT_ITER,
    OBJECT_BIT_INVERT, OBJECT_DIR
    """

    def test_object_str(self):
        code, ns = build_expr([
            ["CONST", "hello"],
            ["OBJECT_STR", 1],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), "hello")


    def test_object_len(self):
        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["OBJECT_LEN", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 3)


    def test_object_not(self):
        code, ns = build_expr([
            ["CONST", False],
            ["OBJECT_NOT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_truth(self):
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_TRUTH", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), False)


    def test_object_type(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["OBJECT_TYPE", 0],
            ["RETURN", 1],
        ])
        self.assertIs(run_to_completion(make_callable(code, ns)()), int)


    def test_object_vars(self):
        class ObjWithDict(object):
            pass
        obj = ObjWithDict()
        obj.x = 1
        code, ns = build_expr([
            ["CONST", obj],
            ["OBJECT_VARS", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, {"x": 1})


    def test_object_aiter(self):
        class AsyncIterable(object):
            def __aiter__(self):
                return self
            def __anext__(self):
                raise StopAsyncIteration
        code, ns = build_expr([
            ["CONST", AsyncIterable()],
            ["OBJECT_AITER", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsNotNone(result)


    def test_object_repr(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["OBJECT_REPR", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), "42")


    def test_object_iter(self):
        code, ns = build_expr([
            ["CONST", [1, 2]],
            ["OBJECT_ITER", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(list(result), [1, 2])


    def test_object_bit_invert(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["OBJECT_BIT_INVERT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), -1)


    def test_object_dir(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["OBJECT_DIR", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, list)
        self.assertIn("real", result)

# The end.
