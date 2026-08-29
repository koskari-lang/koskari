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
tests.reproducers.test_provider_error_paths

The corrective suite from the six-lens review: provider steps that
error after a partial install fully undo themselves (no leaked
elements, no stranded frames, resumption matches builtins); the
filter's rejected-element loop is flat (no C recursion crash);
apply of a generator function constructs the generator; a values
callable through map delivers its computed element on every surface;
and a mapped function's StopIteration truncates with builtins parity
instead of fabricating elements. Layered map/filter compositions
close the file

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import unittest
import weakref

from koskari.lib import KosFilter, KosMap, values
from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


SRC = """
define two-args [a b]:
    a

define inc [x]:
    (+ x 1)

define odd? [x]:
    (== 1 (% x 2))

define gen2 [a b]:
    yield a
    yield b

define consume [it]:
    define total 0
    for-each [v it]:
        set! total (+ total v)
    total

define collect [it]:
    define out (list)
    for-each [v it]:
        (out.append v)
    out

define apply-gen [args]:
    (apply gen2 args)

define apply-gen-tail []:
    (apply gen2 #(7 8))

define apply-partial-gen []:
    (apply (partial gen2 1) #(2))
"""


class Elem:
    pass


def trunc(x):
    if x == 3:
        raise StopIteration
    return x * 10


def trunc_pred(x):
    if x == 3:
        raise StopIteration
    return True


class TestProviderErrorPaths(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        module = new_module("provider_errors")
        vars(module)["__file__"] = "<provider_errors>"
        await load_module_source_async(module, SRC)
        self.v = vars(module)

    # === D1: partial-install error undo ===

    async def test_failing_predicate_leaks_nothing(self):
        items = [Elem(), Elem(), Elem()]
        refs = [weakref.ref(x) for x in items]
        f = KosFilter(self.v["two-args"], iter(items))
        del items

        it = iter(f)
        for _ in range(3):
            with self.assertRaises(TypeError):
                next(it)
        del f, it
        gc.collect()
        self.assertEqual([r() for r in refs], [None, None, None])

    async def test_failing_function_leaks_nothing_map(self):
        items = [Elem(), Elem(), Elem()]
        refs = [weakref.ref(x) for x in items]
        m = KosMap(self.v["two-args"], iter(items))
        del items

        it = iter(m)
        for _ in range(3):
            with self.assertRaises(TypeError):
                next(it)
        del m, it
        gc.collect()
        self.assertEqual([r() for r in refs], [None, None, None])

    async def test_failing_step_resumes_with_builtins_parity(self):
        f1 = lambda a, b: a + b
        f2 = lambda a, b: a * b

        f = KosFilter(values(1, 2), [3, f1, f2])
        with self.assertRaises(TypeError):
            list(f)
        self.assertEqual(list(f), [f1, f2])

    async def test_failing_step_async_drive_resumes(self):
        f1 = lambda a, b: a + b
        f = KosFilter(values(1, 2), [3, f1])
        with self.assertRaises(TypeError):
            await f.__anext__()
        self.assertEqual([x async for x in f], [f1])

    # === D2: flat reject loop over a driven source ===

    async def test_deep_reject_run_demand(self):
        n = 200000
        f = KosFilter(lambda x: x >= n - 1, KosMap(lambda x: x, range(n)))
        self.assertEqual(list(f), [n - 1])

    async def test_deep_reject_run_async(self):
        n = 200000
        f = KosFilter(lambda x: x >= n - 1, KosMap(lambda x: x, range(n)))
        self.assertEqual([x async for x in f], [n - 1])

    # === D3: apply of a generator function constructs ===

    async def test_apply_generator_function(self):
        g = await self.v["apply-gen"]((5, 6))
        self.assertEqual(type(g).__name__, "AsyncGenerator")
        self.assertEqual([x async for x in g], [5, 6])

    async def test_apply_generator_tail_position(self):
        g = await self.v["apply-gen-tail"]()
        self.assertEqual([x async for x in g], [7, 8])

    async def test_apply_partial_over_generator(self):
        result = await self.v["apply-partial-gen"]()
        self.assertEqual(list(result), [1, 2])

    # === D4: values through map delivers on every surface ===

    async def test_values_through_map_demand(self):
        m = KosMap(values(1, 2), [lambda a, b: a + b])
        self.assertEqual(list(m), [3])

    async def test_values_through_map_async(self):
        m = KosMap(values(1, 2), [lambda a, b: a + b])
        self.assertEqual([x async for x in m], [3])

    async def test_values_through_map_kos_consumer(self):
        m = KosMap(values(1, 2), [lambda a, b: a + b])
        self.assertEqual(await self.v["consume"](m), 3)

    async def test_values_as_filter_predicate(self):
        truthy = lambda a, b: a
        m = KosFilter(values(1, 2), [truthy])
        self.assertEqual(list(m), [truthy])

    # === D5: StopIteration truncation parity ===

    async def test_map_stopiteration_truncates_demand(self):
        self.assertEqual(list(KosMap(trunc, [1, 2, 3, 4])),
                         list(map(trunc, [1, 2, 3, 4])))

    async def test_map_stopiteration_truncates_async(self):
        result = [x async for x in KosMap(trunc, [1, 2, 3, 4, 5])]
        self.assertEqual(result, [10, 20])

    async def test_map_stopiteration_truncates_kos_consumer(self):
        result = await self.v["collect"](KosMap(trunc, [1, 2, 3, 4]))
        self.assertEqual(result, [10, 20])

    async def test_filter_stopiteration_truncates(self):
        self.assertEqual(list(KosFilter(trunc_pred, [1, 2, 3, 4])), [1, 2])
        f = KosFilter(trunc_pred, [1, 2, 3, 4, 5])
        self.assertEqual([x async for x in f], [1, 2])

    # === layered map/filter composition ===

    async def test_filter_over_map_kos_fns(self):
        v = self.v
        stack = KosFilter(v["odd?"], KosMap(v["inc"], range(8)))
        self.assertEqual([x async for x in stack], [1, 3, 5, 7])

    async def test_map_over_filter_mixed(self):
        v = self.v
        stack = KosMap(v["inc"], KosFilter(lambda x: x % 2, range(8)))
        self.assertEqual(list(stack), [2, 4, 6, 8])

    async def test_three_deep_stack(self):
        v = self.v
        stack = KosFilter(None,
                          KosMap(lambda x: x if x % 3 else 0,
                                 KosFilter(v["odd?"], range(12))))
        self.assertEqual([x async for x in stack], [1, 5, 7, 11])

    async def test_stack_with_values_layer(self):
        add = lambda a, b: a + b
        stack = KosFilter(lambda g: g(1, 1) == 2,
                          KosMap(values(0, 0), [lambda a, b: add]))
        self.assertEqual(list(stack), [add])

    async def test_stack_kos_consumer(self):
        v = self.v
        stack = KosMap(v["inc"], KosFilter(v["odd?"], range(10)))
        self.assertEqual(await v["consume"](stack), 30)


# The end.
