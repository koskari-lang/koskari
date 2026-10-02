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
tests.reproducers.test_run_until_complete_repro

Reproducer: koskari.asyncio.run_until_complete drove the coroutine with a
non-existent asyncio.run_until_complete module attribute, so every call
raised AttributeError. It should instead run the awaitable to completion on
the given event loop and return the result, converting a defer or Koskari
Coroutine via as_coroutine first.

(run_until_complete is a Python-host adapter, not a Koskari builtin: it calls
loop.run_until_complete, which cannot run on an already-running loop.)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
import unittest
import warnings

import pytest

import koskari.asyncio
from koskari.lib import defer


pytestmark = pytest.mark.phase_reproducers


class TestRunUntilComplete(unittest.TestCase):
    """
    run_until_complete drives an awaitable to completion on a given loop
    """


    def test_defer_value(self):
        """
        A defer holding a plain value resolves to that value
        """

        loop = asyncio.new_event_loop()
        try:
            result = koskari.asyncio.run_until_complete(loop, defer(lambda: 41))
        finally:
            loop.close()
        self.assertEqual(result, 41)


    def test_suspending_coroutine(self):
        """
        A coroutine that actually suspends runs to completion
        """

        async def work():
            await asyncio.sleep(0)
            return 'done'

        loop = asyncio.new_event_loop()
        try:
            result = koskari.asyncio.run_until_complete(loop, work())
        finally:
            loop.close()
        self.assertEqual(result, 'done')


    def test_default_loop(self):
        """
        A None loop falls back to the current event loop
        """

        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            result = koskari.asyncio.run_until_complete(None, defer(lambda: 7))
        finally:
            asyncio.set_event_loop(None)
            loop.close()
        self.assertEqual(result, 7)


    def test_default_loop_when_none_is_set(self):
        """
        With no current loop a None loop makes one and installs it,
        without the implicit-creation deprecation warning
        """

        asyncio.set_event_loop(None)
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                result = koskari.asyncio.run_until_complete(
                    None, defer(lambda: 7))
            self.assertEqual(result, 7)

            # the loop made for the call is now the current one, so a
            # second call reuses it
            loop = asyncio.get_event_loop()
            self.assertFalse(loop.is_closed())
            self.assertEqual(
                koskari.asyncio.run_until_complete(None, defer(lambda: 8)), 8)
            self.assertIs(asyncio.get_event_loop(), loop)
        finally:
            try:
                asyncio.get_event_loop().close()
            except RuntimeError:
                pass
            asyncio.set_event_loop(None)


# The end.
