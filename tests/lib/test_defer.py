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
Unit tests for defer (single-shot await-handle) type.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.lib as lib
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class TestDeferType(unittest.TestCase):
    """
    Tests for defer type registration and construction from Python
    """

    def test_defer_type_exists(self):
        """
        defer type is registered in the module
        """

        self.assertTrue(hasattr(lib, "defer"))
        self.assertEqual(lib.defer.__name__, "defer")


    def test_defer_construct_positional(self):
        """
        defer(fn, a, b) invokes fn and leaves a done defer for sync result
        """

        def fn(x, y):
            return x + y
        d = lib.defer(fn, 1, 2)
        self.assertIsInstance(d, lib.defer)
        self.assertIn("done", repr(d))


    def test_defer_construct_with_kwds(self):
        """
        defer(fn, a, key=val) invokes fn with kwds and completes when sync
        """

        def fn(x, key=0):
            return x + key
        d = lib.defer(fn, 1, key=10)
        self.assertIsInstance(d, lib.defer)
        self.assertIn("done", repr(d))


    def test_defer_construct_single_arg(self):
        """
        defer(fn) invokes fn with no args and completes when sync
        """

        def fn():
            return 42
        d = lib.defer(fn)
        self.assertIsInstance(d, lib.defer)
        self.assertIn("done", repr(d))


    def test_fn_invoked_at_construction(self):
        """
        Deferred function runs when defer is created, not on await
        """

        calls = []

        def track(x):
            calls.append(x)
            return x * 2

        d = lib.defer(track, 21)
        self.assertEqual(calls, [21])
        self.assertIn("done", repr(d))


    def test_construct_raises(self):
        """
        defer(fn) propagates exceptions from fn at construction time
        """

        def fn_raises():
            raise ValueError("oops")

        with self.assertRaises(ValueError) as ctx:
            lib.defer(fn_raises)
        self.assertIn("oops", str(ctx.exception))


    def test_defer_on_defer_is_idempotent(self):
        """
        defer(d) where d is already a defer returns the same instance
        """

        def fn():
            return 42

        d = lib.defer(fn)
        d2 = lib.defer(d)
        self.assertIs(d2, d)


class TestDeferNoAutoAwait(unittest.IsolatedAsyncioTestCase):
    """
    Defer returned from a call is not auto-awaited; it is pushed as a value
    """

    async def test_call_returns_defer_as_value(self):
        """
        Calling (defer fn 42) returns a defer handle, not the bare value 42
        """

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "identity"],
            ["CONST", 42],
            ["CALL", 2],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "identity": lib.identity})
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, lib.defer)
        self.assertIn("done", repr(result))


class TestDeferExplicitAwait(unittest.IsolatedAsyncioTestCase):
    """
    Explicit (await (defer f a b)) resolves the defer handle to a value
    """

    async def test_await_defer_returns_value(self):
        """
        await (defer identity 42) returns 42
        """

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "identity"],
            ["CONST", 42],
            ["CALL", 2],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "identity": lib.identity})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_await_defer_calls_fn_once(self):
        """
        Deferred function is invoked exactly once at defer construction
        """

        calls = []

        def track(x):
            calls.append(x)
            return x * 2

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "track"],
            ["CONST", 21],
            ["CALL", 2],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "track": track})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)
        self.assertEqual(calls, [21])


class TestDeferPythonAwait(unittest.IsolatedAsyncioTestCase):
    """
    Python can await a defer directly (tp_as_async / am_await)
    """

    async def test_python_await_defer_returns_value(self):
        """
        await defer(fn, 1, 2) from Python returns the result
        """

        def fn(x, y):
            return x + y
        d = lib.defer(fn, 1, 2)
        result = await d
        self.assertEqual(result, 3)


    async def test_python_await_defer_construct_raises(self):
        """
        defer(fn_raises) raises at construction; await is not required
        """

        def fn_raises():
            raise ValueError("oops")

        with self.assertRaises(ValueError) as ctx:
            lib.defer(fn_raises)
        self.assertIn("oops", str(ctx.exception))


    async def test_python_await_defer_of_defer(self):
        """
        await defer(g, defer(f, 1)) works from Python
        """

        def f(x):
            return x + 10

        def g(d):
            return lib.defer(lambda: 99)

        inner = lib.defer(f, 1)
        outer = lib.defer(g, inner)
        result = await outer
        self.assertEqual(result, 99)


class TestDeferOfDefer(unittest.IsolatedAsyncioTestCase):
    """
    Defer-of-defer collapses; outer adopt inner, single handle
    """

    async def test_await_defer_of_defer_returns_value(self):
        """
        (await (defer g (defer f 1))) runs g and f, returns final value
        """

        def f(x):
            return x + 10

        def g(d):
            return lib.defer(lambda: 99)

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "g"],
            ["VAR", "defer"],
            ["VAR", "f"],
            ["CONST", 1],
            ["CALL", 2],
            ["CALL", 2],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "f": f, "g": g})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


class TestDeferSingleShot(unittest.IsolatedAsyncioTestCase):
    """
    The first await consumes a defer; reuse raises RuntimeError
    """

    async def test_double_python_await_raises(self):
        """
        Second Python await of a delegating defer raises RuntimeError
        """

        async def work():
            return 7

        d = lib.defer(work)
        self.assertEqual(await d, 7)
        self.assertIn("spent", repr(d))
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            await d


    async def test_double_koskari_await_raises(self):
        """
        Second koskari AWAIT of the same defer raises RuntimeError
        """

        async def work():
            return 7

        code, ns = build_expr([
            ["VAR", "defer"],
            ["VAR", "work"],
            ["CALL", 1],
            ["DUP", 1],
            ["AWAIT", 1],
            ["POP", 1],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"defer": lib.defer, "work": work})
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            await make_callable(code, ns)()


    async def test_born_done_defer_single_shot(self):
        """
        A defer born DONE yields its value once, then raises on reuse
        """

        d = lib.defer(lambda: 42)
        self.assertIn("done", repr(d))
        self.assertEqual(await d, 42)
        self.assertIn("spent", repr(d))
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            await d


    async def test_defer_of_spent_defer_is_identity(self):
        """
        defer(spent) returns the same spent handle; await raises
        """

        d = lib.defer(lambda: 1)
        await d
        d2 = lib.defer(d)
        self.assertIs(d2, d)
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            await d2


    async def test_adopt_of_spent_donor(self):
        """
        A deferred call returning a spent defer adopts the spent state
        """

        d1 = lib.defer(lambda: 1)
        await d1
        d2 = lib.defer(lambda: d1)
        self.assertIn("spent", repr(d2))
        self.assertIn("moved_from", repr(d1))
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            await d2


    async def test_adopt_of_moved_from_donor(self):
        """
        Adopting a moved-from donor leaves a defined moved-from handle
        """

        d1 = lib.defer(lambda: 5)
        d2 = lib.defer(lambda: d1)    # moves d1's payload into d2
        self.assertIn("moved_from", repr(d1))

        d3 = lib.defer(lambda: d1)    # donor has nothing left to give
        self.assertIn("moved_from", repr(d3))
        with self.assertRaisesRegex(RuntimeError, "after move"):
            await d3

        self.assertEqual(await d2, 5)


class Suspend:
    """
    Awaitable that yields once (generator-based, has send/throw/close)
    """

    def __await__(self):
        yield "suspended"


class SendlessAwaitable:
    """
    Awaitable whose __await__ returns a plain send-less iterator
    """

    def __init__(self, items):
        self.items = items


    def __await__(self):
        return iter(self.items)


class ThrowlessIter:
    """
    Iterator with send but no throw; close records invocation
    """

    def __init__(self):
        self.closed = False


    def send(self, value):
        return "yielded"


    def close(self):
        self.closed = True


class ThrowlessAwaitable:
    """
    Awaitable whose __await__ iterator lacks throw
    """

    def __init__(self):
        self.iter = ThrowlessIter()


    def __await__(self):
        return self.iter


class TestDeferCoroutineProtocol(unittest.IsolatedAsyncioTestCase):
    """
    defer carries the full coroutine protocol
    """

    def test_passes_abc_coroutine_hook(self):
        """
        defer passes the structural collections.abc.Coroutine check
        """

        import asyncio
        import collections.abc

        async def work():
            return 1

        d = lib.defer(work)
        self.assertTrue(asyncio.iscoroutine(d))
        self.assertTrue(isinstance(d, collections.abc.Coroutine))
        d.close()


    def test_iter_raises_next_drives(self):
        """
        iter(d) is a TypeError (no tp_iter); next(d) drives the handle
        """

        d = lib.defer(lambda: 42)
        with self.assertRaises(TypeError):
            iter(d)
        with self.assertRaises(StopIteration) as caught:
            next(d)
        self.assertEqual(caught.exception.value, 42)
        self.assertIn("spent", repr(d))


    async def test_stopiteration_payload_tuple(self):
        """
        A tuple value survives await intact (StopIteration instance fix)
        """

        self.assertEqual(await lib.defer(lambda: (1, 2)), (1, 2))


    async def test_stopiteration_payload_exception_instance(self):
        """
        A StopIteration instance value survives await intact
        """

        payload = StopIteration("x")
        result = await lib.defer(lambda: payload)
        self.assertIs(result, payload)


    def test_send_matrix(self):
        """
        send: DONE consumes; non-None to DONE is a TypeError; dead raises
        """

        d = lib.defer(lambda: 7)
        with self.assertRaisesRegex(TypeError, "just-started"):
            d.send(1)
        with self.assertRaises(StopIteration) as caught:
            d.send(None)
        self.assertEqual(caught.exception.value, 7)
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            d.send(None)


    def test_send_lazily_starts_delegation(self):
        """
        send on a DELEGATING defer starts the iterator and forwards
        """

        async def work():
            return 9

        d = lib.defer(work)
        self.assertIn("delegating", repr(d))
        with self.assertRaises(StopIteration) as caught:
            d.send(None)
        self.assertEqual(caught.exception.value, 9)
        self.assertIn("spent", repr(d))


    def test_send_non_none_to_sendless_iterator(self):
        """
        Non-None send to a send-less __await__ iterator is CPython's
        AttributeError for the missing send, raised at the delegation
        point, so it ends the delegation and spends the handle
        """

        d = lib.defer(lambda: SendlessAwaitable(["a"]))
        self.assertEqual(d.send(None), "a")
        self.assertIn("iterating", repr(d))
        with self.assertRaises(AttributeError):
            d.send("x")
        self.assertIn("spent", repr(d))


    def test_throw_matrix(self):
        """
        throw: DONE drops value and raises; dead raises reuse error
        """

        d = lib.defer(lambda: 7)
        with self.assertRaisesRegex(ValueError, "boom"):
            d.throw(ValueError("boom"))
        self.assertIn("spent", repr(d))
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            d.throw(ValueError)


    def test_throw_into_delegate_with_one_argument_throw(self):
        """
        A delegate whose throw takes the modern single argument is
        thrown into with the exception instance alone
        """

        seen = []

        class It:
            def __iter__(self):
                return self

            def __next__(self):
                return "y"

            def throw(self, exc):
                seen.append(exc)
                raise StopIteration("handled")

        class Aw:
            def __await__(self):
                return It()

        d = lib.defer(Aw)
        self.assertEqual(d.send(None), "y")
        with self.assertRaises(StopIteration) as caught:
            d.throw(ValueError, ValueError("boom"))
        self.assertEqual(caught.exception.value, "handled")
        self.assertIsInstance(seen[0], ValueError)
        self.assertIn("spent", repr(d))

    def test_generator_exit_closes_a_throwless_delegate(self):
        """
        GeneratorExit thrown into a delegate with close but no throw
        closes it first, an error from that close propagating, as the
        yield-from rule has it
        """

        class It:
            def __iter__(self):
                return self

            def __next__(self):
                return "y"

            def close(self):
                raise KeyError("close failed")

        class Aw:
            def __await__(self):
                return It()

        d = lib.defer(Aw)
        self.assertEqual(d.send(None), "y")
        with self.assertRaises(KeyError):
            d.throw(GeneratorExit)
        self.assertIn("spent", repr(d))

    def test_throw_into_delegating_coroutine(self):
        """
        throw on a DELEGATING defer forwards into the inner coroutine
        """

        async def work():
            return 1

        d = lib.defer(work)
        with self.assertRaises(ValueError):
            d.throw(ValueError)
        self.assertIn("spent", repr(d))


    def test_throw_caught_by_inner(self):
        """
        An inner that catches the thrown error completes normally
        """

        async def work():
            try:
                await Suspend()
            except ValueError:
                return "caught"

        d = lib.defer(work)
        d.send(None)    # run to the suspension point
        with self.assertRaises(StopIteration) as caught:
            d.throw(ValueError)
        self.assertEqual(caught.exception.value, "caught")


    def test_throwless_iterator_raises_at_defer(self):
        """
        A throw-less delegate raises the exception at the defer, SPENT
        """

        inner = ThrowlessAwaitable()
        d = lib.defer(lambda: inner)
        self.assertEqual(d.send(None), "yielded")
        with self.assertRaises(ValueError):
            d.throw(ValueError)
        self.assertIn("spent", repr(d))
        self.assertFalse(inner.iter.closed)


    def test_generatorexit_closes_throwless_iterator(self):
        """
        GeneratorExit into a throw-less delegate calls its close first
        """

        inner = ThrowlessAwaitable()
        d = lib.defer(lambda: inner)
        self.assertEqual(d.send(None), "yielded")
        with self.assertRaises(GeneratorExit):
            d.throw(GeneratorExit)
        self.assertTrue(inner.iter.closed)
        self.assertIn("spent", repr(d))


    def test_close_matrix(self):
        """
        close: DONE and dead states are quiet no-ops that consume
        """

        d = lib.defer(lambda: 7)
        self.assertIsNone(d.close())
        self.assertIn("spent", repr(d))
        self.assertIsNone(d.close())

        d2 = lib.defer(lambda: d)     # adopt moves d into MOVED_FROM
        self.assertIn("moved_from", repr(d))
        self.assertIsNone(d.close())
        self.assertIsNone(d2.close())


    def test_close_unstarted_coroutine_no_warning(self):
        """
        close on a DELEGATING defer closes the held coroutine quietly
        """

        import gc
        import warnings

        async def work():
            return 1

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            d = lib.defer(work)
            self.assertIsNone(d.close())
            del d
            gc.collect()
        messages = [str(w.message) for w in caught]
        self.assertFalse(
            [m for m in messages if "never awaited" in m], messages)


    def test_close_closeless_inner(self):
        """
        close on a DELEGATING defer over a close-less inner is a no-op
        """

        d = lib.defer(lambda: SendlessAwaitable([]))
        self.assertIsNone(d.close())
        self.assertIn("spent", repr(d))


    def test_close_runs_inner_finally(self):
        """
        close on an ITERATING defer runs the inner's finally block
        """

        ran = []

        async def work():
            try:
                await Suspend()
            finally:
                ran.append("finally")

        d = lib.defer(work)
        d.send(None)
        self.assertIsNone(d.close())
        self.assertEqual(ran, ["finally"])


    def test_close_ignored_generatorexit_propagates(self):
        """
        An inner swallowing GeneratorExit surfaces the RuntimeError
        """

        async def stubborn():
            try:
                await Suspend()
            except GeneratorExit:
                pass
            # still alive after swallowing the close: triggers the
            # RuntimeError; a later GC close then succeeds normally
            await Suspend()

        d = lib.defer(stubborn)
        d.send(None)
        with self.assertRaisesRegex(RuntimeError, "ignored GeneratorExit"):
            d.close()
        self.assertIn("spent", repr(d))


    async def test_adopt_of_iterating_donor(self):
        """
        Adopting an ITERATING donor resumes it to completion
        """

        async def work():
            return 11

        d1 = lib.defer(work)
        self.assertIs(d1.__await__(), d1)
        self.assertIn("iterating", repr(d1))

        d2 = lib.defer(lambda: d1)
        self.assertIn("moved_from", repr(d1))
        self.assertIn("iterating", repr(d2))
        self.assertEqual(await d2, 11)


class TestDeferReentryFromRelease(unittest.TestCase):
    """
    A defer's state transitions release the payload of the state they
    leave. That release can run a __del__ that reaches the same defer,
    so the new state is written before the old payload goes; the
    re-entry then meets a consistent defer instead of a NULL payload
    """

    def test_inner_del_reentering_during_start(self):
        holder = {}
        seen = []

        def it_gen():
            yield 1
            return 2

        class Inner:
            def __await__(self):
                return it_gen()

            def __del__(self):
                d = holder.get("d")
                if d is not None:
                    try:
                        seen.append(("sent", d.send(None)))
                    except BaseException as e:
                        seen.append(("raised", type(e).__name__))

        d = lib.defer(lambda: Inner())
        holder["d"] = d
        outcomes = []
        for _ in range(3):
            try:
                outcomes.append(("sent", d.send(None)))
            except StopIteration as e:
                outcomes.append(("stop", e.value))
                break
            except RuntimeError as e:
                outcomes.append(("runtime", str(e)))
                break
        holder.clear()
        self.assertTrue(seen, "the inner's __del__ never re-entered")
        self.assertIn(("stop", 2), outcomes + seen)

    def test_iterator_del_reentering_during_spend(self):
        holder = {}
        seen = []

        class Iter:
            def __init__(self):
                self.n = 0

            def __iter__(self):
                return self

            def __next__(self):
                self.n += 1
                if self.n > 1:
                    raise StopIteration(5)
                return None

            def __del__(self):
                d = holder.get("d")
                if d is not None:
                    try:
                        seen.append(("sent", d.send(None)))
                    except BaseException as e:
                        seen.append(("raised", type(e).__name__))

        class Inner:
            def __await__(self):
                return Iter()

        d = lib.defer(lambda: Inner())
        holder["d"] = d
        self.assertIsNone(d.send(None))
        with self.assertRaises(StopIteration) as done:
            d.send(None)
        self.assertEqual(5, done.exception.value)
        holder.clear()
        self.assertEqual([("raised", "RuntimeError")], seen)


# The end.
