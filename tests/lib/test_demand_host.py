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
tests.lib.test_demand_host

One demand wrapper called many times from Python: every call returns
its own value, a call that re-enters the same wrapper through Python
while it is running still completes, each call sees the parameterize
state in force at that call, and a call that raised leaves the wrapper
usable.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

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
"""


def pymap(d, items):
    return list(map(d, items))


class TestDemandHost(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("demand_host")
        vars(module)["__file__"] = "<demand_host>"
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


# The end.
