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
tests.importer.test_await_foreign_exception

Reproducer: a foreign (Python/asyncio) awaitable that suspends to the
real event loop and then raises must have its exception re-injected at
the Koskari await point, where the frame's try/except handler chain can
catch it. See design/archive/await-foreign-exception-drop.md.

The value path (suspend then return) works; the raise-after-suspension
path escapes every enclosing Koskari try/except because the coroutine
send/throw resume path drops the inner exception instead of re-entering
the evaluator with it.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio

from koskari.lib import drive_async_loop
from tests.compile.specials import SpecialsTestCase

import pytest


pytestmark = pytest.mark.phase_basics


async def _ok():
    await asyncio.sleep(0)
    return 42


async def _boom():
    await asyncio.sleep(0)
    raise EOFError()


async def _boom_nosuspend():
    raise EOFError()


TRY_AWAIT_SRC = """
try:
  (await (make_coro))
except [EOFError]:
  "caught-eof"
except [Exception as: e]:
  "caught-other"
"""


class TestAwaitForeignException(SpecialsTestCase):
    """
    Exceptions from foreign awaitables raise at the Koskari await point
    """

    def drive(self, make_coro, src=TRY_AWAIT_SRC, **defns):
        expr, _mod = self.compile_expr(
            src, make_coro=make_coro,
            EOFError=EOFError, Exception=Exception,
            **defns)

        async def outer():
            return await expr()

        return drive_async_loop(outer())


    def test_value_after_suspend(self):
        """
        Foreign coroutine returns after a loop suspension: value delivered
        """

        self.assertEqual(42, self.drive(lambda: _ok()))


    def test_exc_no_suspend(self):
        """
        Foreign coroutine raises without suspending: caught by except
        """

        self.assertEqual('caught-eof', self.drive(lambda: _boom_nosuspend()))


    def test_exc_after_suspend(self):
        """
        Foreign coroutine raises after a loop suspension: caught by except
        """

        self.assertEqual('caught-eof', self.drive(lambda: _boom()))


    def test_exc_after_suspend_bare_except(self):
        """
        Foreign raise after suspension is caught by a bare except clause
        """

        src = """
        try:
          (await (make_coro))
        except [Exception as: e]:
          "caught-other"
        """
        self.assertEqual('caught-other', self.drive(lambda: _boom(), src=src))


    def test_finally_runs_on_foreign_raise(self):
        """
        try/finally cleanup runs when a foreign awaitable raises
        """

        events = []

        src = """
        try:
          (await (make_coro))
        except [EOFError]:
          "caught-eof"
        finally:
          (note "cleanup")
        """
        result = self.drive(lambda: _boom(), src=src, note=events.append)
        self.assertEqual('caught-eof', result)
        self.assertEqual(['cleanup'], events)


    def test_cancellation_runs_finally(self):
        """
        Cancelling a coroutine parked on a foreign awaitable runs finally
        """

        events = []

        src = """
        try:
          (await (make_coro))
        finally:
          (note "cleanup")
        """
        expr, _mod = self.compile_expr(
            src, make_coro=lambda: asyncio.sleep(3600), note=events.append)

        async def run():
            return await expr()

        async def outer():
            task = asyncio.get_event_loop().create_task(run())
            await asyncio.sleep(0)
            await asyncio.sleep(0)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                return 'cancelled'
            return 'not-cancelled'

        self.assertEqual('cancelled', drive_async_loop(outer()))
        self.assertEqual(['cleanup'], events)


# The end.
