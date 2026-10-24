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
tests.lib.test_demand_cached_context

One demand wrapper called many times from Python runs each call on a
context of its own: every call returns its own value, a call that
re-enters the same wrapper through Python while it is running still
completes, each call sees the parameterize state in force at that call,
a call that raised leaves the wrapper usable, and a root parameter
store made by one call is read by a later call, through the same
wrapper or another. Calls carrying a keyword argument behave the same:
repeated, raising, reentrant, and escaping through call/cc. A
parameterize extent entered inside a boundary call stays in that call,
and a coroutine created inside it runs under the state at its creation
after the extent and the call have exited.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest
from functools import partial

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define-parameter p 0)

(define square (lambda [x] (* x x)))

(define read_p (lambda [x] (+ p x)))

(define countdown (lambda [d via n]
  (if (== n 0) 0 (+ 1 (via d (- n 1))))))

(define both_batches (lambda [d pymap]
  (#list
    (parameterize [[p 10]] (pymap d #[1 2]))
    (parameterize [[p 20]] (pymap d #[1 2])))))

(define fragile (lambda [x] (if (== x 0) (raise (ValueError "zero")) x)))

(define-parameter cc_root)

(define swap_root (lambda [v]
  (define-global cc_root)
  (define old (parameterq cc_root None))
  (set! cc_root v)
  old))

(define store_root (lambda [v]
  (define-global cc_root)
  (set! cc_root v)
  v))

(define read_root (lambda [] cc_root))

(define addkw (lambda [a b: 1] (+ (* a 10) b)))

(define failkw (lambda [a b: 0]
  (if (== b 0) (raise (ValueError a)) (+ a b))))

(define nestkw (lambda [d via n step: 1]
  (if (== n 0) 0 (+ step (via d n)))))

(define escapekw (lambda [a b: 0]
  (+ 1 (call/cc (lambda [k] (call k (+ a b)) (raise (ValueError "late")))))))

(define read_p0 (lambda [] p))

(define shadow_in (lambda [x] (parameterize [[p 20]] (+ p x))))

(define outer_check (lambda [d_shadow d_read pycall]
  (parameterize [[p 10]]
    #((pycall d_shadow 1) (pycall d_read 1) p))))

(define defer_in (lambda [] (parameterize [[p 20]] (defer read_p0))))

(define outer_defer (lambda [d_make d_read pycall]
  (parameterize [[p 10]]
    #((pycall d_make) (pycall d_read 1) p))))
"""


def pymap(d, items):
    return list(map(d, items))


def pycall(d, *args):
    return d(*args)


class TestDemandCachedContext(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("demand_cached_context")
        vars(module)["__file__"] = "<demand_cached_context>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)


    async def test_repeated_calls_return_their_own_values(self):
        mod = await self.load()
        d = lib.demand(mod["square"])
        self.assertEqual(pymap(d, range(6)), [0, 1, 4, 9, 16, 25])
        self.assertEqual(pymap(d, range(3)), [0, 1, 4])


    async def test_reentry_through_python_while_running(self):
        mod = await self.load()
        seen = []

        def via(d, n):
            seen.append(n)
            return d(d, via, n)

        d = lib.demand(mod["countdown"])
        self.assertEqual(d(d, via, 4), 4)
        self.assertEqual(seen, [3, 2, 1, 0])
        self.assertEqual(d(d, via, 2), 2)


    async def test_each_call_sees_the_parameters_in_force(self):
        mod = await self.load()
        d = lib.demand(mod["read_p"])
        self.assertEqual(await mod["both_batches"](d, pymap),
                         [[11, 12], [21, 22]])
        self.assertEqual(pymap(d, [1]), [1])


    async def test_wrapper_survives_a_raising_call(self):
        mod = await self.load()
        d = lib.demand(mod["fragile"])
        with self.assertRaises(ValueError):
            d(0)
        self.assertEqual(d(5), 5)
        with self.assertRaises(ValueError):
            d(0)
        self.assertEqual(pymap(d, [1, 2]), [1, 2])


    async def test_root_store_persists_across_calls(self):
        mod = await self.load()
        self.addCleanup(lib.ParamRef("cc_root").clear)
        d = lib.demand(mod["swap_root"])
        self.assertIsNone(d(1))
        self.assertEqual(d(2), 1)
        self.assertEqual(lib.ParamRef("cc_root").deref(), 2)


    async def test_root_store_shared_between_wrappers(self):
        mod = await self.load()
        self.addCleanup(lib.ParamRef("cc_root").clear)
        store = lib.demand(mod["store_root"])
        read = lib.demand(mod["read_root"])
        self.assertEqual(store(7), 7)
        self.assertEqual(read(), 7)


    async def test_repeated_keyword_calls(self):
        mod = await self.load()
        d = lib.demand(mod["addkw"])
        self.assertEqual([d(a, b=a + 1) for a in range(5)],
                         [1, 12, 23, 34, 45])
        self.assertEqual(list(map(partial(d, b=7), range(4))),
                         [7, 17, 27, 37])
        self.assertEqual(d(3), 31)
        self.assertEqual(d(a=2, b=5), 25)


    async def test_keyword_call_raise_leaves_wrapper_usable(self):
        mod = await self.load()
        d = lib.demand(mod["failkw"])
        for a in (1, 2):
            with self.assertRaises(ValueError) as caught:
                d(a, b=0)
            self.assertEqual(caught.exception.args, (a,))
            self.assertEqual(d(a, b=4), a + 4)
        self.assertEqual(list(map(partial(d, b=1), [1, 2])), [2, 3])


    async def test_keyword_call_reentry_through_python(self):
        mod = await self.load()
        seen = []

        def via(d, n):
            seen.append(n)
            return d(d, via, n - 1, step=n)

        d = lib.demand(mod["nestkw"])
        self.assertEqual(d(d, via, 3, step=1), 6)
        self.assertEqual(seen, [3, 2, 1])
        self.assertEqual(d(d, via, 2, step=10), 12)
        self.assertEqual(seen, [3, 2, 1, 2, 1])


    async def test_keyword_call_call_cc_escape(self):
        mod = await self.load()
        d = lib.demand(mod["escapekw"])
        self.assertEqual(d(1, b=2), 4)
        self.assertEqual(d(10, b=20), 31)
        self.assertEqual(lib.demand(mod["addkw"])(4, b=2), 42)
        self.assertEqual(d(5, b=5), 11)


    async def test_callee_extent_stays_in_the_callee(self):
        mod = await self.load()
        d_shadow = lib.demand(mod["shadow_in"])
        d_read = lib.demand(mod["read_p"])
        self.assertEqual(await mod["outer_check"](d_shadow, d_read, pycall),
                         (21, 11, 10))


    async def test_coroutine_made_in_a_callee_extent(self):
        mod = await self.load()
        d_make = lib.demand(mod["defer_in"])
        d_read = lib.demand(mod["read_p"])
        result = await mod["outer_defer"](d_make, d_read, pycall)
        self.assertEqual(result[1:], (11, 10))
        self.assertEqual(await result[0], 20)


# The end.
