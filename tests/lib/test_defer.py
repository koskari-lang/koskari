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
Unit tests for defer (single-shot await-handle) type.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import koskari.lib as lib
from tests import make_callable
from tests.lib.opcode import build_expr, run_to_completion


class TestDeferType(unittest.IsolatedAsyncioTestCase):
    """Tests for defer type registration and construction from Python."""

    async def test_defer_type_exists(self):
        """defer type is registered in the module."""

        self.assertTrue(hasattr(lib, "defer"))
        self.assertEqual(lib.defer.__name__, "defer")

    async def test_defer_construct_positional(self):
        """defer(fn, a, b) creates a pending defer."""

        def fn(x, y):
            return x + y
        d = lib.defer(fn, 1, 2)
        self.assertIsInstance(d, lib.defer)
        self.assertIn("pending", repr(d))

    async def test_defer_construct_with_kwds(self):
        """defer(fn, a, key=val) creates a pending defer with kwds."""

        def fn(x, key=0):
            return x + key
        d = lib.defer(fn, 1, key=10)
        self.assertIsInstance(d, lib.defer)
        self.assertIn("pending", repr(d))

    async def test_defer_construct_single_arg(self):
        """defer(fn) creates a pending defer with no args."""

        def fn():
            return 42
        d = lib.defer(fn)
        self.assertIsInstance(d, lib.defer)
        self.assertIn("pending", repr(d))


class TestDeferNoAutoAwait(unittest.IsolatedAsyncioTestCase):
    """Defer returned from a call is not auto-awaited; it is pushed as a value."""

    async def test_call_returns_defer_as_value(self):
        """Calling (defer fn 42) returns a defer, not the result of fn(42)."""

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "identity"],
            ["CONST", 42],
            ["CALL", 2],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "identity": lib.identity})
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, lib.defer)
        self.assertIn("pending", repr(result))


class TestDeferExplicitAwait(unittest.IsolatedAsyncioTestCase):
    """Explicit (await (defer f a b)) runs f(a,b) once and returns the value."""

    async def test_await_defer_returns_value(self):
        """await (defer identity 42) returns 42."""

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "identity"],
            ["CONST", 42],
            ["CALL", 2],
            ["AWAIT", 0],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "identity": lib.identity})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)

    async def test_await_defer_calls_fn_once(self):
        """Deferred function is invoked exactly once on first await."""

        calls = []

        def track(x):
            calls.append(x)
            return x * 2

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "track"],
            ["CONST", 21],
            ["CALL", 2],
            ["AWAIT", 0],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "track": track})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)
        self.assertEqual(calls, [21])


class TestDeferOfDefer(unittest.IsolatedAsyncioTestCase):
    """Defer-of-defer collapses; outer adopt inner, single handle."""

    async def test_await_defer_of_defer_returns_value(self):
        """(await (defer g (defer f 1))) runs g and f, returns final value."""

        def f(x):
            return x + 10

        def g(d):
            return lib.defer(lambda: 99)  # return another defer

        # g(defer(f, 1)) -> defer(g, defer(f, 1)); await runs g(defer(f,1))
        # which returns defer(lambda: 99). Adopt then await that -> 99.
        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "g"],
            ["VAR", "defer"],
            ["VAR", "f"],
            ["CONST", 1],
            ["CALL", 2],
            ["CALL", 2],
            ["AWAIT", 0],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "f": f, "g": g})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


# The end.
