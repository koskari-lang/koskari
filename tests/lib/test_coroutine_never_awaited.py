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
tests.lib.test_coroutine_never_awaited

A Koskari coroutine dropped before it was ever started warns with
RuntimeWarning "coroutine '<qualname>' was never awaited", as CPython
does. One closed before start, awaited from Python, awaited from
Koskari, or run through demand does not warn, and neither does an
async generator dropped unstarted.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest
import warnings

import pytest

from koskari.lib import demand_call
from koskari.module import load_module_source_async, new_module
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


SRC = """
(define [add a b] (+ a b))

(define [await_it c] (await c))

(define [produce n] (for-each [i (range n)] (yield i)))
"""


def never_awaited(caught):
    return [
        str(w.message) for w in caught
        if issubclass(w.category, RuntimeWarning)
        and "never awaited" in str(w.message)]


class TestCoroutineNeverAwaited(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("coroutine_never_awaited")
        vars(module)["__file__"] = "<coroutine_never_awaited>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)


    async def test_dropped_unstarted_coroutine_warns(self):
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            del coro
            gc.collect()
        self.assertEqual(never_awaited(caught),
                         ["coroutine 'add' was never awaited"])


    async def test_each_dropped_coroutine_of_a_function_warns(self):
        # the second call revives the first one's record
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            for _ in range(3):
                coro = add(1, 2)
                del coro
            gc.collect()
        self.assertEqual(never_awaited(caught),
                         ["coroutine 'add' was never awaited"] * 3)


    async def test_dropped_in_a_cycle_warns(self):
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            cycle = [coro]
            cycle.append(cycle)
            del coro, cycle
            gc.collect()
        self.assertEqual(never_awaited(caught),
                         ["coroutine 'add' was never awaited"])


    async def test_closed_before_start_does_not_warn(self):
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            coro.close()
            del coro
            gc.collect()
        self.assertEqual(never_awaited(caught), [])


    async def test_awaited_from_python_does_not_warn(self):
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            self.assertEqual(await coro, 3)
            del coro
            gc.collect()
        self.assertEqual(never_awaited(caught), [])


    async def test_sent_from_python_does_not_warn(self):
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            with self.assertRaises(StopIteration) as stop:
                coro.send(None)
            self.assertEqual(stop.exception.value, 3)
            del coro, stop
            gc.collect()
        self.assertEqual(never_awaited(caught), [])


    async def test_awaited_from_koskari_does_not_warn(self):
        mod = await self.load()
        add = mod["add"]
        await_it = mod["await_it"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            self.assertEqual(await await_it(coro), 3)
            del coro
            gc.collect()
        self.assertEqual(never_awaited(caught), [])


    async def test_run_through_demand_does_not_warn(self):
        mod = await self.load()
        add = mod["add"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = add(1, 2)
            self.assertEqual(coro.demand(), 3)
            del coro
            self.assertEqual(demand_call(add, 1, 2), 3)
            gc.collect()
        self.assertEqual(never_awaited(caught), [])


    async def test_dropped_unstarted_async_generator_does_not_warn(self):
        mod = await self.load()
        produce = mod["produce"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            agen = produce(3)
            del agen
            gc.collect()
        self.assertEqual(never_awaited(caught), [])


    async def test_dropped_unstarted_expression_warns_with_its_code_name(self):
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]], name="answer")
        expr = make_callable(code, ns)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            coro = expr()
            del coro
            gc.collect()
        self.assertEqual(never_awaited(caught),
                         ["coroutine 'answer' was never awaited"])


# The end.
