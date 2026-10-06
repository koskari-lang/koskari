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
tests.reproducers.test_with_return_poll

A return out of a with block installs the manager's __exit__ through
the native call entry. That install is a cooperation point, and when
the eval breaker is up it reports so. The unwind walk must still store
the return on the with node so the value is delivered once __exit__
finishes. The signal is raised from inside the body, so the install is
the first cooperation point that sees it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import signal
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import _thread

define-class Mgr []:
    define __enter__ [self]:
        self

    define __exit__ [self typ val tb]:
        False

define return-under-pending-signal []:
    with [(Mgr)]:
        (_thread.interrupt_main)
        (return 42)
    "fell through"
"""


class TestWithReturnPoll(unittest.IsolatedAsyncioTestCase):

    @classmethod
    def setUpClass(cls):
        async def load():
            module = new_module("with_return_poll")
            vars(module)["__file__"] = "<with_return_poll>"
            await load_module_source_async(module, SRC)
            return module

        cls.module = asyncio.run(load())


    def setUp(self):
        super().setUp()
        self.hits = []
        self._prior = signal.signal(
            signal.SIGINT, lambda signum, frame: self.hits.append(signum))


    def tearDown(self):
        signal.signal(signal.SIGINT, self._prior)
        super().tearDown()


    async def test_return_is_delivered_and_signal_taken(self):
        fn = vars(self.module)["return-under-pending-signal"]
        self.assertEqual(await fn(), 42)
        self.assertEqual(self.hits, [signal.SIGINT])


# The end.
