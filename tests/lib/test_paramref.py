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

import pytest

from koskari.lib import Domain, ParamRef, Ref, parameter, symbol
from tests import make_callable
from tests.lib.opcode import build_expr


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
    ParamRef behavior outside any with Domain() and any koskari_eval:
    the thread's default Domain answers
    """

    def test_deref_no_domain_returns_default(self):
        """
        deref of an unbound name outside a with returns the default value
        """

        p = ParamRef("x", 99)
        self.assertEqual(p.deref(), 99)

    def test_deref_no_domain_no_default_raises(self):
        """
        deref of an unbound name outside a with, with no default, raises
        NameError
        """

        p = ParamRef("x")
        with self.assertRaises(NameError):
            p.deref()

    def test_store_no_domain_lands_in_thread_default(self):
        """
        store outside a with lands in the thread default's root scope
        """

        p = ParamRef("paramref_no_domain_store", 0)
        self.addCleanup(p.clear)
        p.store(42)
        self.assertEqual(p.deref(), 42)
        self.assertFalse(p.empty)

    def test_clear_no_domain_empties_the_binding(self):
        """
        clear outside a with empties the thread default's binding, and
        the default answers again
        """

        p = ParamRef("paramref_no_domain_clear", 0)
        p.store(42)
        p.clear()
        self.assertTrue(p.empty)
        self.assertEqual(p.deref(), 0)

    def test_empty_no_domain_is_true(self):
        """
        ParamRef.empty is True for a name the thread default never bound
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
        deref before any store returns default; after store returns stored
        value. A bare same-key ref checks that "returns the default" means
        unbound rather than bound to that value.
        """

        p = ParamRef("y", 99)
        p_bare = ParamRef("y")

        def before_after_store():
            with self.assertRaises(NameError):
                p_bare.deref()
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


    def test_parameter_ref_builtin_absent(self):
        """
        parameter-ref is not part of the surface: ParamRef is the only
        way to address a parameter
        """

        import koskari.builtins as builtins
        self.assertNotIn("parameter-ref", vars(builtins))


    async def test_deref_default_does_not_store(self):
        """
        A default answers the ref that declares it and never lands in a
        store, so a same-key ParamRef with no default of its own still
        raises NameError.
        """

        p_with = ParamRef("nonsticky", 42)
        p_bare = ParamRef("nonsticky")

        def default_flow():
            first = p_with.deref()
            try:
                p_bare.deref()
                second = "no-raise"
            except NameError:
                second = "unbound"
            return (first, second)

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": default_flow})
        result = await make_callable(code, ns)()
        self.assertEqual(result, (42, "unbound"))


    async def test_deref_miss_creates_no_cell(self):
        """
        A read that misses leaves both the shadow table and the root
        scope untouched.
        """

        p = ParamRef("untouched", "fallback")
        seen = []

        def read_then_inspect():
            value = p.deref()
            seen.append(value)
            seen.append(Domain.current_domain().param_scopes())

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": read_then_inspect})
        await make_callable(code, ns)()

        self.assertEqual(seen[0], "fallback")
        self.assertNotIn("untouched", seen[1]["shadow"])
        self.assertNotIn("untouched", seen[1]["root"])


    async def test_store_lands_in_root_scope(self):
        """
        With no enclosing parameterize extent, a store creates its cell
        in the root scope rather than the context's shadow table.
        """

        p = ParamRef("rooted")
        seen = []

        def store_then_inspect():
            p.store("v")
            seen.append(Domain.current_domain().param_scopes())

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store_then_inspect})
        await make_callable(code, ns)()

        self.assertNotIn("rooted", seen[0]["shadow"])
        self.assertEqual(seen[0]["root"]["rooted"].deref(), "v")


    async def test_parameter_fn_default_is_local_fallback(self):
        """
        parameter(name, default) is a value query: the fallback answers
        the caller and never lands in the store. A later parameter(name)
        with no default still finds nothing.
        """

        def query_flow():
            first = parameter("fn_fallback", 7)
            try:
                parameter("fn_fallback")
                second = "no-raise"
            except NameError:
                second = "unbound"
            return (first, second)

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": query_flow})
        result = await make_callable(code, ns)()
        self.assertEqual(result, (7, "unbound"))


    async def test_deref_no_binding_no_default_raises_nameerror(self):
        """
        deref inside an active domain with no binding and no default raises
        NameError.
        """

        p = ParamRef("unbound_key")

        def deref_unbound():
            with self.assertRaises(NameError):
                p.deref()
            return True

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": deref_unbound})
        self.assertTrue(await make_callable(code, ns)())


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
