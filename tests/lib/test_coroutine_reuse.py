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
Unit tests for KosCoroutine reuse semantics.

A completed Koskari Coroutine behaves like a finished Python coroutine:
send and throw raise RuntimeError instead of a silent bare StopIteration;
close stays a no-op returning None.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


def _const_coro(value=42):
    code, ns = build_expr([
        ["CONST", value],
        ["RETURN", 1],
    ])
    return make_callable(code, ns)()


class TestCoroutineReuse(unittest.IsolatedAsyncioTestCase):
    """
    Completed coroutines are single-shot
    """

    async def test_double_await_raises(self):
        """
        Awaiting a completed Coroutine again raises RuntimeError
        """

        coro = _const_coro()
        self.assertEqual(await coro, 42)
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited coroutine"):
            await coro


    async def test_send_after_completion_raises(self):
        """
        send() on a completed Coroutine raises RuntimeError
        """

        coro = _const_coro()
        self.assertEqual(await coro, 42)
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited coroutine"):
            coro.send(None)


    async def test_throw_after_completion_raises(self):
        """
        throw() on a completed Coroutine raises RuntimeError
        """

        coro = _const_coro()
        self.assertEqual(await coro, 42)
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited coroutine"):
            coro.throw(ValueError)


    async def test_close_after_completion_returns_none(self):
        """
        close() on a completed Coroutine remains a no-op
        """

        coro = _const_coro()
        self.assertEqual(await coro, 42)
        self.assertIsNone(coro.close())


# The end.
