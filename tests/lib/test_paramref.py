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
Unit tests for ParamRef

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import ParamRef, Ref
from koskari.lib import symbol
from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestParamRefCreate(unittest.TestCase):
    """
    ParamRef creation and basic properties
    """

    def test_create_with_key_and_default(self):
        """
        ParamRef can be created with key and default value
        """

        p = ParamRef("x", 42)
        self.assertIsInstance(p, ParamRef)
        self.assertIsInstance(p, Ref)
        self.assertEqual(p.name, "x")

    def test_create_with_key_only(self):
        """
        ParamRef can be created with key only
        """

        p = ParamRef("foo")
        self.assertIsInstance(p, ParamRef)
        self.assertEqual(p.name, "foo")

    def test_create_with_symbol_key(self):
        """
        ParamRef can be created with a symbol key and normalizes to string name
        """

        p = ParamRef(symbol("x"))
        self.assertEqual(p.name, "x")

    def test_create_with_invalid_key_raises_typeerror(self):
        """
        ParamRef rejects non-string non-symbol keys
        """

        with self.assertRaises(TypeError):
            ParamRef(123)


class TestParamRefNoDomain(unittest.TestCase):
    """
    ParamRef behavior outside active domain (no koskari_eval)
    """

    def test_deref_no_domain_returns_default(self):
        """
        deref outside domain returns default value
        """

        p = ParamRef("x", 99)
        self.assertEqual(p.deref(), 99)

    def test_deref_no_domain_no_default_raises(self):
        """
        deref outside domain with no default raises RuntimeError
        """

        p = ParamRef("x")
        with self.assertRaises(RuntimeError):
            p.deref()

    def test_store_no_domain_raises(self):
        """
        store outside domain raises RuntimeError
        """

        p = ParamRef("x", 0)
        with self.assertRaises(RuntimeError):
            p.store(42)

    def test_clear_no_domain_raises(self):
        """
        clear outside domain raises RuntimeError
        """

        p = ParamRef("x", 0)
        with self.assertRaises(RuntimeError):
            p.clear()

    def test_empty_no_domain_is_true(self):
        """
        ParamRef.empty is always True outside an active domain.
        """

        self.assertTrue(ParamRef("x").empty)
        self.assertTrue(ParamRef("x", 123).empty)


class TestParamRefInsideScope(unittest.IsolatedAsyncioTestCase):
    """
    ParamRef store/deref/clear inside active domain (Python callback during
    eval)
    """

    async def test_store_and_deref_same_value(self):
        """
        Store value and deref during eval returns same value. Uses a Python
        callback invoked from bytecode; domain is active during the call.
        """

        p = ParamRef("test_key", None)

        def store_and_deref():
            p.store(123)
            return p.deref()

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store_and_deref})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 123)


    async def test_two_paramrefs_same_key_share_value(self):
        """
        Two ParamRefs with same key see the same value within a thread.
        One stores, the other derefs.
        """

        p1 = ParamRef("shared", 0)
        p2 = ParamRef("shared", 0)

        def store_via_p1_deref_via_p2():
            p1.store(777)
            return p2.deref()

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store_via_p1_deref_via_p2})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 777)


    async def test_clear_then_deref_returns_default(self):
        """
        After store, clear makes empty; deref returns default.
        """
        p = ParamRef("x", 88)

        def store_clear_deref():
            p.store(100)
            p.clear()
            return p.deref()

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store_clear_deref})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 88)


    async def test_deref_before_store_returns_default(self):
        """
        deref before any store returns default; after store returns stored value.
        """

        p = ParamRef("y", 99)

        def before_after_store():
            before = p.deref()
            p.store(1)
            after = p.deref()
            return (before, after)

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": before_after_store})
        result = await make_callable(code, ns)()
        self.assertEqual(result[0], 99)
        self.assertEqual(result[1], 1)


    async def test_store_overwrites_previous(self):
        """
        store overwrites previous value; deref returns latest.
        """
        p = ParamRef("z", 0)

        def overwrite():
            p.store(10)
            p.store(20)
            p.store(30)
            return p.deref()

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": overwrite})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 30)


    async def test_empty_tracks_binding_state(self):
        """
        ParamRef.empty follows active-domain binding state.
        """

        p = ParamRef("emptiness", 0)

        def check_empty_flow():
            before = p.empty
            p.store(5)
            after_store = p.empty
            p.clear()
            after_clear = p.empty
            return (before, after_store, after_clear)

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": check_empty_flow})
        result = await make_callable(code, ns)()
        self.assertEqual(result, (True, False, True))


# The end.
