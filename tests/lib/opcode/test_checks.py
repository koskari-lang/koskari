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
Tests for CHECK_EQ, CHECK_NE, CHECK_IN, CHECK_IS, CHECK_IS_INSTANCE opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestCheckOpcodes(CevalTest):
    """
    Tests for CHECK_EQ, CHECK_NE, CHECK_IN, CHECK_NOT_IN, CHECK_IS,
    CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS
    """

    def test_check_eq_jumps_on_mismatch(self):
        """
        CHECK_EQ jumps when left != right
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CHECK_EQ", "fail"],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_eq_continues_on_match(self):
        """
        CHECK_EQ does not jump when left == right
        """
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 10],
            ["CHECK_EQ", "fail"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_check_in_jumps_when_not_contained(self):
        """
        CHECK_IN jumps when value not in container
        """
        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["CONST", 99],
            ["CHECK_IN", "miss"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_in_continues_when_contained(self):
        """
        CHECK_IN does not jump when value in container
        """
        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["CONST", 2],
            ["CHECK_IN", "miss"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_check_is_jumps_when_different_identity(self):
        """
        CHECK_IS jumps when left is not right
        """
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", []],
            ["CHECK_IS", "diff"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_is_continues_when_same_identity(self):
        """
        CHECK_IS does not jump when left is right
        """
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["CHECK_IS", "diff"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_check_is_instance_jumps_when_false(self):
        """
        CHECK_IS_INSTANCE jumps when isinstance(value, cls) is False
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", str],
            ["CHECK_IS_INSTANCE", "no"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_is_instance_continues_when_true(self):
        """
        CHECK_IS_INSTANCE does not jump when isinstance(value, cls) is True
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", int],
            ["CHECK_IS_INSTANCE", "no"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


# The end.
