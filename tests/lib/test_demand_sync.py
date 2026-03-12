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
Tests for demand_sync: invoke Koskari callables synchronously without suspension.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import KoskariSyncViolation, demand_sync
from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestDemandSyncExpression(unittest.TestCase):
    """
    demand_sync on Expression (no args)
    """

    def test_return_value(self):
        """
        demand_sync(expr) returns value for CONST RETURN
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        self.assertEqual(demand_sync(expr), 42)


    def test_raise_propagation(self):
        """
        demand_sync(expr) propagates RAISE
        """
        code, ns = build_expr(
            [
                ["VAR", "exc"],
                ["RAISE", 1],
                ["RETURN", 0],
            ],
            values={"exc": ValueError("demand_sync raise test")},
        )
        expr = make_callable(code, ns)
        with self.assertRaises(ValueError) as ctx:
            demand_sync(expr)
        self.assertEqual(ctx.exception.args, ("demand_sync raise test",))


    def test_yield_violation(self):
        """
        demand_sync(expr) raises KoskariSyncViolation on YIELD
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_sync(expr)
        self.assertIn("suspension", str(ctx.exception))



class TestDemandSyncPlainPython(unittest.TestCase):
    """
    demand_sync on plain Python callables
    """

    def test_plain_python_callable(self):
        """
        demand_sync(lambda: 99) returns 99
        """
        self.assertEqual(demand_sync(lambda: 99), 99)


    def test_plain_python_raising(self):
        """
        demand_sync(lambda: 1/0) propagates ZeroDivisionError
        """
        with self.assertRaises(ZeroDivisionError):
            demand_sync(lambda: 1 / 0)


# The end.
