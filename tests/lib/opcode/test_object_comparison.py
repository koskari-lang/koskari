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
Tests for OBJECT_EQ opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestObjectComparisonOpcodes(CevalTest):
    """
    Tests for OBJECT_EQ, OBJECT_NE, OBJECT_LT, OBJECT_LE,
    OBJECT_GT, OBJECT_GE, OBJECT_IN, OBJECT_NOT_IN, OBJECT_IS,
    OBJECT_IS_NOT, OBJECT_IS_INSTANCE, OBJECT_IS_SUBCLASS
    """

    def test_object_eq(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", 42],
            ["OBJECT_EQ", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_ne(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["OBJECT_NE", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_lt(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["OBJECT_LT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_le(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 2],
            ["OBJECT_LE", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_gt(self):
        code, ns = build_expr([
            ["CONST", 3],
            ["CONST", 2],
            ["OBJECT_GT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_ge(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 2],
            ["OBJECT_GE", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_in(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", [1, 2, 3]],
            ["OBJECT_IN", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_not_in(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", [1, 2, 3]],
            ["OBJECT_NOT_IN", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is(self):
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["OBJECT_IS", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is_not(self):
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", []],
            ["OBJECT_IS_NOT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is_instance(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", int],
            ["OBJECT_IS_INSTANCE", 1],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is_subclass(self):
        code, ns = build_expr([
            ["CONST", bool],
            ["CONST", int],
            ["OBJECT_IS_SUBCLASS", 1],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


# The end.

# The end.
