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


class TestCoroutineThrow(unittest.IsolatedAsyncioTestCase):
    """
    throw() and close() on a coroutine that has not completed
    """

    async def test_throw_class_raises_it(self):
        coro = _const_coro()
        with self.assertRaises(ValueError):
            coro.throw(ValueError)


    async def test_throw_class_and_value_raises_with_args(self):
        coro = _const_coro()
        with self.assertRaises(ValueError) as ctx:
            coro.throw(ValueError, "boom")
        self.assertEqual(ctx.exception.args, ("boom",))


    async def test_throw_instance_raises_that_instance(self):
        coro = _const_coro()
        exc = ValueError("boom")
        with self.assertRaises(ValueError) as ctx:
            coro.throw(exc)
        self.assertIs(ctx.exception, exc)


    async def test_throw_does_not_leak_the_normalized_instance(self):
        """
        throw(Cls) instantiates Cls during normalization; the instance
        must be released once the throw is over. It used to be leaked.
        """

        import gc
        import weakref

        class Exit(BaseException):
            pass

        coro = _const_coro()
        try:
            coro.throw(Exit)
        except Exit as e:
            ref = weakref.ref(e)
            e = None
        gc.collect()
        self.assertIsNone(ref())


    async def test_throw_keeps_the_value_alive(self):
        """
        throw(Cls, val) must not drop a reference to val it never took;
        the raised exception still owns it after the throw
        """

        import sys

        coro = _const_coro()
        value = "a value object owned only by this test"
        before = sys.getrefcount(value)
        with self.assertRaises(ValueError) as ctx:
            coro.throw(ValueError, value)
        self.assertIs(ctx.exception.args[0], value)
        del ctx
        self.assertEqual(sys.getrefcount(value), before)


    async def test_close_before_start_finishes_the_coroutine(self):
        coro = _const_coro()
        self.assertIsNone(coro.close())
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited coroutine"):
            await coro



class TestCoroutineReentrancy(unittest.TestCase):
    """
    koskari_eval is not re-entrant on one context. send refuses a
    second drive while one is in flight; throw and close reach the
    same evaluator and must refuse it too, including the await
    iterator they touch on the way there.
    """


    def make_coro(self, callback):
        """
        A coroutine whose only act is to call callback, so the
        callback runs inside the drive
        """

        code, ns = build_expr(
            [["VAR", "cb"], ["CALL", 0], ["RETURN", 1]],
            local_syms=("cb",),
            values={"cb": callback},
        )
        return make_callable(code, ns)()


    def test_throw_during_send_is_refused(self):
        """
        throw from inside the drive raises rather than nesting a
        second koskari_eval on the live context
        """

        box = {}
        caught = []

        def callback():
            try:
                box['coro'].throw(ValueError("x"))
            except BaseException as exc:
                caught.append(exc)
            return 7

        box["coro"] = self.make_coro(callback)
        with self.assertRaises(StopIteration):
            box["coro"].send(None)

        self.assertEqual(len(caught), 1)
        self.assertIsInstance(caught[0], RuntimeError)
        self.assertIn("already being awaited", str(caught[0]))


    def test_close_during_send_is_refused(self):
        """
        close from inside the drive is refused before it can close
        the stored await iterator
        """

        box = {}
        caught = []

        def callback():
            try:
                box['coro'].close()
                caught.append(None)
            except BaseException as exc:
                caught.append(exc)
            return 7

        box["coro"] = self.make_coro(callback)
        with self.assertRaises(StopIteration):
            box["coro"].send(None)

        self.assertEqual(len(caught), 1)
        self.assertIsInstance(caught[0], RuntimeError)
        self.assertIn("already being awaited", str(caught[0]))



# The end.
