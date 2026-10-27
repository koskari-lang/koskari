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
tests.reproducers.test_signal_generic_install

A frame install is a cooperation point: with the eval breaker up it
answers POLL so the handler polls before the callee's first
instruction. The native call entries used by the demand path and by
the native providers must translate that answer, since their callers
read it as a call outcome. Each test raises a signal from inside
Koskari with a handler that returns, then runs a call whose callee has
a rest parameter, which takes the generic install arm: Python's sorted
callinga demand-wrapped key with no Koskari install between, and a native
filter stepped by a for-each, whose step is reached through the
prologue and so polls first. Each checks the value and that the
handler ran by the loop after it

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import signal
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import _thread
import-from builtins sorted

var key-fn (demand (lambda [x *: rest] x))

def sort-under-signal []:
    (_thread.interrupt_main)
    var result (sorted #[3 1 2] key: key-fn)
    var n 0
    while (< n 1):
        setq n (+ n 1)
    result

def filter-under-signal []:
    (_thread.interrupt_main)
    var acc #[]
    for-each [v (filter (lambda [x *: rest] (> x 1)) #[1 2 3])]:
        (acc.append v)
    var n 0
    while (< n 1):
        setq n (+ n 1)
    acc
"""


class TestSignalGenericInstall(unittest.IsolatedAsyncioTestCase):
    """
    Generic-arm installs with a signal pending and a returning handler
    """

    def setUp(self):
        self.hits = []
        self._prior = signal.signal(
            signal.SIGINT, lambda signum, frame: self.hits.append(signum))

    def tearDown(self):
        signal.signal(signal.SIGINT, self._prior)

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_demand_call_generic_arm(self):
        mod = await self.load("sgi_demand")
        result = await vars(mod)["sort-under-signal"]()
        self.assertEqual(result, [1, 2, 3])
        self.assertEqual(self.hits, [signal.SIGINT])

    async def test_provider_call_generic_arm(self):
        mod = await self.load("sgi_provider")
        result = await vars(mod)["filter-under-signal"]()
        self.assertEqual(result, [2, 3])
        self.assertEqual(self.hits, [signal.SIGINT])


# The end.
