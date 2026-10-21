# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Unit tests for parameter

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import ParamRef, parameter, symbol
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class TestParameterNoDomain(unittest.TestCase):
    """
    parameter behavior outside any with Domain() and any koskari_eval:
    the thread's default Domain answers
    """

    def test_parameter_no_domain_returns_default(self):
        """
        parameter outside domain returns default value when provided
        """

        self.assertEqual(parameter("x", 99), 99)

    def test_parameter_symbol_key_no_domain_returns_default(self):
        """
        parameter accepts symbol key outside domain
        """

        self.assertEqual(parameter(symbol("x"), 99), 99)

    def test_parameter_no_domain_no_default_raises(self):
        """
        parameter of an unbound name outside a with, with no default,
        raises NameError
        """

        with self.assertRaises(NameError):
            parameter("x")

    def test_parameter_no_domain_reads_thread_default(self):
        """
        parameter outside a with reads a store made in the thread
        default
        """

        ref = ParamRef("parameter_no_domain_key")
        self.addCleanup(ref.clear)
        ref.store("stored")
        self.assertEqual(parameter("parameter_no_domain_key"), "stored")
        self.assertEqual(parameter("parameter_no_domain_key", 1), "stored")

    def test_parameter_non_string_non_symbol_raises_typeerror(self):
        """
        parameter rejects non-string non-symbol keys
        """

        with self.assertRaises(TypeError):
            parameter(123, 1)


class TestParameterInsideScope(unittest.IsolatedAsyncioTestCase):
    """
    parameter behavior with active domain during eval
    """

    async def test_parameter_returns_value_when_bound(self):
        """
        After ParamRef store, parameter returns stored value
        """

        p = ParamRef(symbol("test_key"), None)

        def store_then_fetch():
            p.store(123)
            return (parameter("test_key"), parameter(symbol("test_key")))

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store_then_fetch})
        result = await make_callable(code, ns)()
        self.assertEqual(result[0], 123)
        self.assertEqual(result[1], 123)

    async def test_parameter_unbound_returns_default(self):
        """
        parameter returns provided default when key is unbound
        """

        def fetch_default():
            return parameter("missing", 42)

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": fetch_default})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)

    async def test_parameter_unbound_no_default_raises(self):
        """
        parameter raises NameError when key is unbound and default not provided
        """

        def fetch_missing():
            return parameter("missing")

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": fetch_missing})
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


# The end.

