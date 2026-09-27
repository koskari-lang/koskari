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
tests.lib.test_defer_vectorcall

Constructing a defer from a call site: positional and keyword arguments
reach the deferred function unchanged from Koskari and from Python, a
missing function is a TypeError, a defer of a defer is the same defer,
and the deferred function's own error propagates from the construction.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define add (lambda [a b c: 0] (+ a b c)))

(define with_keyword (lambda [] (await (defer add 1 2 c: 3))))

(define positional_only (lambda [] (await (defer add 4 5))))

(define no_function (lambda [] (defer)))

(define same_defer (lambda [d] (defer d)))

(define pass_through (lambda [f] (await (defer f 1 2 3 4 5 6 7 8 9 10 11 12))))
"""


class TestDeferVectorcall(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("defer_vectorcall")
        vars(module)["__file__"] = "<defer_vectorcall>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_keyword_argument_reaches_the_function(self):
        mod = await self.load()
        self.assertEqual(await mod["with_keyword"](), 6)

    async def test_positional_arguments_reach_the_function(self):
        mod = await self.load()
        self.assertEqual(await mod["positional_only"](), 9)

    async def test_missing_function_is_a_type_error(self):
        mod = await self.load()
        with self.assertRaises(TypeError):
            await mod["no_function"]()

    async def test_defer_of_a_defer_is_the_same_defer(self):
        mod = await self.load()
        d = lib.defer(lambda: 1)
        self.assertIs(await mod["same_defer"](d), d)

    async def test_many_positional_arguments_arrive_in_order(self):
        mod = await self.load()
        seen = []

        def collect(*args):
            seen.append(args)
            return len(args)

        self.assertEqual(await mod["pass_through"](collect), 12)
        self.assertEqual(seen, [tuple(range(1, 13))])

    def test_python_keywords_and_positionals_together(self):
        def fn(a, b, *rest, key=None, **extra):
            return (a, b, rest, key, extra)

        d = lib.defer(fn, 1, 2, 3, key="k", other=4)
        with self.assertRaises(StopIteration) as stop:
            d.send(None)
        self.assertEqual(stop.exception.value, (1, 2, (3,), "k", {"other": 4}))

    def test_python_function_error_propagates_from_construction(self):
        def bad():
            raise KeyError("missing")

        with self.assertRaises(KeyError):
            lib.defer(bad)

    def test_python_missing_function_is_a_type_error(self):
        with self.assertRaises(TypeError):
            lib.defer()
        with self.assertRaises(TypeError):
            lib.defer(fn=len)


# The end.
