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
tests.reproducers.test_asyncgen_protocol

CPython parity for the async-generator protocol methods. Every scenario
runs twice -- once against a native ``async def`` generator and once
against the equivalent Koskari generator -- and the outcomes must
match. CPython is the oracle: no expected value here is hand-written,
so a Koskari behavior difference cannot hide behind a wrong
expectation.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

define g-plain []:
    (yield 1)
    (yield 2)

define g-echo []:
    define got (yield 1)
    (yield got)

define g-catches []:
    try:
        (yield 1)
        (yield "unreached")
    except [ValueError]:
        (yield "caught")

define g-catch-return []:
    try:
        (yield 1)
        (yield "unreached")
    except [ValueError]:
        None

define g-await-echo []:
    (await (sleep 0.001))
    define got (yield 1)
    (yield got)

define g-cleanup [col]:
    try:
        (yield "ready")
    finally:
        (await (sleep 0.001))
        (col.append "cleaned")

define g-ignores-exit []:
    try:
        (yield 1)
    except [GeneratorExit]:
        (yield "ignored")

define g-handler-await []:
    try:
        (yield 1)
    except [ValueError]:
        (await (sleep 0.001))
        (yield "handled")

define g-param-extent [col]:
    try:
        (parameterize [[pval "inside"]]
            (yield (parameterq pval "no-extent")))
    finally:
        (col.append (parameterq pval "no-extent"))

define g-fin-sync [col]:
    try:
        (yield 1)
        (yield 2)
    finally:
        (col.append "cleaned")

define g-slow [col]:
    try:
        (yield 1)
        (await (sleep 60))
        (yield 2)
    finally:
        (col.append "cleaned")
"""


async def p_plain():
    yield 1
    yield 2


async def p_echo():
    got = yield 1
    yield got


async def p_catches():
    try:
        yield 1
        yield "unreached"
    except ValueError:
        yield "caught"


async def p_catch_return():
    try:
        yield 1
        yield "unreached"
    except ValueError:
        return


async def p_await_echo():
    import asyncio
    await asyncio.sleep(0.001)
    got = yield 1
    yield got


async def p_cleanup(col):
    import asyncio
    try:
        yield "ready"
    finally:
        await asyncio.sleep(0.001)
        col.append("cleaned")


async def p_ignores_exit():
    try:
        yield 1
    except GeneratorExit:
        yield "ignored"


async def p_handler_await():
    import asyncio
    try:
        yield 1
    except ValueError:
        await asyncio.sleep(0.001)
        yield "handled"


async def p_slow(col):
    import asyncio
    try:
        yield 1
        await asyncio.sleep(60)
        yield 2
    finally:
        col.append("cleaned")


def s_plain():
    yield 1
    yield 2


def s_fin(col):
    try:
        yield 1
        yield 2
    finally:
        col.append("cleaned")


def s_ignores_exit():
    try:
        yield 1
    except GeneratorExit:
        yield "ignored"


async def outcome(awaitable):
    """
    Collapse an await into a comparable (kind, payload) pair.
    """

    try:
        return ("value", await awaitable)
    except BaseException as exc:
        return ("raise", type(exc).__name__, str(exc))


def sync_outcome(thunk):
    """
    Collapse a call into a comparable (kind, payload) pair.
    """

    try:
        return ("value", thunk())
    except BaseException as exc:
        return ("raise", type(exc).__name__, str(exc))


class TestAsyncGenParity(unittest.IsolatedAsyncioTestCase):
    """
    Identical drive sequences against CPython and Koskari async
    generators produce identical outcomes.
    """

    @classmethod
    def setUpClass(cls):
        import asyncio

        async def load():
            module = new_module("asyncgen_parity")
            vars(module)["__file__"] = "<asyncgen_parity>"
            await load_module_source_async(module, SRC)
            return module

        cls.module = asyncio.run(load())


    def kos(self, name, *args):
        return vars(self.module)[name](*args)


    async def parity(self, python_gen, koskari_name, scenario):
        expected = await scenario(python_gen)
        actual = await scenario(self.kos(koskari_name))
        self.assertEqual(actual, expected)


    async def test_asend_echo(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.asend("ping")),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_echo(), "g-echo", scenario)


    async def test_asend_non_none_just_started(self):
        async def scenario(agen):
            first = await outcome(agen.asend("early"))
            second = await outcome(agen.asend(None))
            return [first, second]

        await self.parity(p_echo(), "g-echo", scenario)


    async def test_athrow_caught_yields(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.athrow(ValueError("boom"))),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_catches(), "g-catches", scenario)


    async def test_athrow_caught_then_return(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.athrow(ValueError("boom"))),
            ]

        await self.parity(p_catch_return(), "g-catch-return", scenario)


    async def test_athrow_uncaught(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.athrow(KeyError("boom"))),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_plain(), "g-plain", scenario)


    async def test_athrow_never_started(self):
        async def scenario(agen):
            return [
                await outcome(agen.athrow(ValueError("early"))),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_plain(), "g-plain", scenario)


    async def test_athrow_exhausted(self):
        async def scenario(agen):
            results = [await outcome(agen.asend(None))]
            results.append(await outcome(agen.asend(None)))
            results.append(await outcome(agen.asend(None)))
            results.append(await outcome(agen.athrow(ValueError("late"))))
            return results

        await self.parity(p_plain(), "g-plain", scenario)


    async def test_asend_across_await(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.asend("ping")),
            ]

        await self.parity(p_await_echo(), "g-await-echo", scenario)


    async def test_athrow_await_in_handler(self):
        """
        the except handler awaits before yielding: the throw-flavored
        drive suspends to the loop and resumes correctly
        """

        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.athrow(ValueError("boom"))),
            ]

        await self.parity(p_handler_await(), "g-handler-await", scenario)


    async def test_athrow_finally_await(self):
        """
        an unhandled athrow whose unwind awaits inside finally still
        runs the cleanup and propagates
        """

        async def scenario(agen, col):
            out = [
                await outcome(agen.asend(None)),
                await outcome(agen.athrow(ValueError("boom"))),
            ]
            out.append(tuple(col))
            return out

        pcol = []
        expected = await scenario(p_cleanup(pcol), pcol)
        kcol = []
        actual = await scenario(self.kos("g-cleanup", kcol), kcol)
        self.assertEqual(actual, expected)


    async def test_aclose_suspended(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.aclose()),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_plain(), "g-plain", scenario)


    async def test_aclose_never_started(self):
        async def scenario(agen):
            return [
                await outcome(agen.aclose()),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_plain(), "g-plain", scenario)


    async def test_aclose_idempotent(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.aclose()),
                await outcome(agen.aclose()),
            ]

        await self.parity(p_plain(), "g-plain", scenario)


    async def test_aclose_finally_await(self):
        """
        an await inside finally is honored during aclose: the cleanup
        suspends to the loop and completes
        """

        async def scenario(agen, col):
            out = [
                await outcome(agen.asend(None)),
                await outcome(agen.aclose()),
            ]
            out.append(tuple(col))
            return out

        pcol = []
        expected = await scenario(p_cleanup(pcol), pcol)
        kcol = []
        actual = await scenario(self.kos("g-cleanup", kcol), kcol)
        self.assertEqual(actual, expected)


    async def test_aclose_ignored_generator_exit(self):
        """
        a body that catches GeneratorExit and yields makes aclose raise
        RuntimeError
        """

        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.aclose()),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_ignores_exit(), "g-ignores-exit", scenario)


    async def test_contextlib_aclosing(self):
        """
        contextlib.aclosing drives our aclose on scope exit
        """

        import contextlib
        if not hasattr(contextlib, "aclosing"):
            self.skipTest("contextlib.aclosing requires Python 3.10")

        async def scenario(agen, col):
            async with contextlib.aclosing(agen) as g:
                first = await outcome(g.__anext__())
            return [first, tuple(col)]

        pcol = []
        expected = await scenario(p_cleanup(pcol), pcol)
        kcol = []
        actual = await scenario(self.kos("g-cleanup", kcol), kcol)
        self.assertEqual(actual, expected)


    async def test_athrow_generator_exit_explicit(self):
        async def scenario(agen):
            return [
                await outcome(agen.asend(None)),
                await outcome(agen.athrow(GeneratorExit())),
                await outcome(agen.asend(None)),
            ]

        await self.parity(p_plain(), "g-plain", scenario)


class TestAcloseKoskariExtent(unittest.IsolatedAsyncioTestCase):
    """
    Koskari-specific aclose behavior with no Python analog: the dynamic
    extent unwinds before outer cleanup runs.
    """

    @classmethod
    def setUpClass(cls):
        TestAsyncGenParity.setUpClass()
        cls.module = TestAsyncGenParity.module


    async def test_aclose_restores_extent(self):
        """
        aclose unwinds the parameterize extent before the enclosing
        finally observes the parameter
        """

        col = []
        agen = vars(self.module)["g-param-extent"](col)
        self.assertEqual(await agen.asend(None), "inside")
        self.assertIsNone(await agen.aclose())
        self.assertEqual(col, ["no-extent"])


    async def test_aclose_after_demand_step(self):
        """
        aclose after a sync demand step closes the generator for both
        drive paths
        """

        agen = vars(self.module)["g-plain"]()
        dgen = iter(agen)
        self.assertEqual(next(dgen), 1)
        self.assertIsNone(await agen.aclose())
        with self.assertRaises(StopIteration):
            next(dgen)


class TestStepperCloseParity(unittest.IsolatedAsyncioTestCase):
    """
    Closing one step's awaitable closes the generator through the
    demand-path teardown: close cascades are terminal, so cleanup runs
    eagerly instead of deferring to GC.

    These are declared-contract tests, not oracle parity: this is
    CPython 3.13+ behavior, applied uniformly across supported
    versions. CPython 3.12 and earlier leave the generator open here;
    the divergence is recorded in the design doc.
    """

    @classmethod
    def setUpClass(cls):
        TestAsyncGenParity.setUpClass()
        cls.module = TestAsyncGenParity.module


    def kos(self, name, *args):
        return vars(self.module)[name](*args)


    async def test_close_undriven_stepper(self):
        """
        closing a never-driven __anext__ awaitable closes the
        generator
        """

        agen = self.kos("g-plain")
        stepper = agen.__anext__()
        self.assertIsNone(stepper.close())
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)


    async def test_reuse_closed_stepper(self):
        """
        driving a stepper after closing it raises RuntimeError
        """

        agen = self.kos("g-plain")
        stepper = agen.__anext__()
        stepper.close()
        with self.assertRaises(RuntimeError) as ctx:
            await stepper
        self.assertIn("cannot reuse", str(ctx.exception))


    async def test_close_runs_cleanup(self):
        """
        closing a stepper runs the suspended generator's finally
        blocks at close time
        """

        col = []
        agen = self.kos("g-fin-sync", col)
        self.assertEqual(await agen.__anext__(), 1)

        stepper = agen.__anext__()
        stepper.close()

        self.assertEqual(col, ["cleaned"])
        self.assertIsNone(await agen.aclose())


    async def test_close_midflight_stepper(self):
        """
        a stepper abandoned mid-await releases and tears down the
        generator; cleanup runs at close time and a later aclose is a
        quiet None
        """

        col = []
        agen = self.kos("g-slow", col)
        self.assertEqual(await agen.__anext__(), 1)

        stepper = agen.__anext__()
        pending = stepper.send(None)
        self.assertIsNotNone(pending)
        stepper.close()

        self.assertEqual(col, ["cleaned"])
        self.assertIsNone(await agen.aclose())


    async def test_drop_midflight_without_close(self):
        """
        dropping the generator and its pending stepper without any
        close call still runs the cleanup at dealloc
        """

        import gc

        col = []
        agen = self.kos("g-slow", col)
        self.assertEqual(await agen.__anext__(), 1)

        stepper = agen.__anext__()
        pending = stepper.send(None)
        self.assertIsNotNone(pending)

        del pending
        del stepper
        del agen
        gc.collect()

        self.assertEqual(col, ["cleaned"])


class TestDemandGenParity(unittest.TestCase):
    """
    Identical drive sequences against CPython sync generators and
    Koskari KosPyIters produce identical outcomes.
    """

    @classmethod
    def setUpClass(cls):
        TestAsyncGenParity.setUpClass()
        cls.module = TestAsyncGenParity.module


    def kos_iter(self, name, *args):
        return iter(vars(self.module)[name](*args))


    def parity(self, python_gen, koskari_name, scenario):
        self.assertEqual(
            scenario(self.kos_iter(koskari_name)),
            scenario(python_gen))


    def test_close_suspended(self):
        def scenario(gen):
            return [
                sync_outcome(lambda: next(gen)),
                sync_outcome(gen.close),
                sync_outcome(lambda: next(gen)),
            ]

        self.parity(s_plain(), "g-plain", scenario)


    def test_close_never_started(self):
        def scenario(gen):
            return [
                sync_outcome(gen.close),
                sync_outcome(lambda: next(gen)),
            ]

        self.parity(s_plain(), "g-plain", scenario)


    def test_close_runs_finally(self):
        def scenario(gen, col):
            out = [
                sync_outcome(lambda: next(gen)),
                sync_outcome(gen.close),
            ]
            out.append(tuple(col))
            return out

        pcol = []
        expected = scenario(s_fin(pcol), pcol)
        kcol = []
        actual = scenario(self.kos_iter("g-fin-sync", kcol), kcol)
        self.assertEqual(actual, expected)


    def test_close_ignored_generator_exit(self):
        def scenario(gen):
            return [
                sync_outcome(lambda: next(gen)),
                sync_outcome(gen.close),
            ]

        self.parity(s_ignores_exit(), "g-ignores-exit", scenario)


    def test_contextlib_closing(self):
        import contextlib

        def scenario(gen, col):
            with contextlib.closing(gen) as g:
                first = sync_outcome(lambda: next(g))
            return [first, tuple(col)]

        pcol = []
        expected = scenario(s_fin(pcol), pcol)
        kcol = []
        actual = scenario(self.kos_iter("g-fin-sync", kcol), kcol)
        self.assertEqual(actual, expected)


# The end.
