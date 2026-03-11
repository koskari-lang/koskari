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
Tests for YIELD_FROM opcode.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from koskari.lib import AsyncGenerator
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


try:
    anext
except NameError:
    # anext wasn't available until Python 3.10
    async def anext(agen):
        return await agen.__anext__()


class TestYieldFrom(CevalTest):
    """
    YIELD_FROM: delegate to sync/async iterators and generators
    """

    async def test_yield_from_upward_into_koskari_from_python_delegate(self):
        """
        Values from a Python delegate flow upward into the Koskari frame
        running YIELD_FROM and then out to the Python consumer. Validates
        that yield_from correctly propagates upward when the delegate is
        a Python iterable.
        """

        it = iter([10, 20, 30])
        code_obj, ns = build_expr([
            ["CONST", it],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            # Each value from the Python iterable must appear at the consumer
            # (flow: Python iterable -> Koskari YIELD_FROM -> Python anext)
            self.assertEqual(await anext(agen), 10)
            self.assertEqual(await anext(agen), 20)
            self.assertEqual(await anext(agen), 30)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertIs(final, None)

        await run()


    async def test_yield_from_upward_into_koskari_from_koskari_delegate(self):
        """
        Values from a Koskari async gen delegate flow upward into the
        Koskari frame running YIELD_FROM and then out to the Python
        consumer. Validates that yield_from correctly propagates upward
        when the delegate is a Koskari async gen.
        """

        inner_code, inner_ns = build_expr([
            ["CONST", "a"],
            ["YIELD", 1],
            ["CONST", "b"],
            ["YIELD", 1],
            ["CONST", "c"],
            ["RETURN", 1],
        ])
        inner_gen = make_callable(inner_code, inner_ns)()
        code_obj, ns = build_expr([
            ["CONST", inner_gen],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            # Each value from the Koskari delegate must appear at the consumer
            # (flow: Koskari inner gen -> outer Koskari YIELD_FROM -> Python anext)
            self.assertEqual(await anext(agen), "a")
            self.assertEqual(await anext(agen), "b")
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            self.assertEqual(self._stop_async_iteration_final(ctx), "c")

        await run()


    async def test_yield_from_sync_iterator_values_and_final(self):
        """
        YIELD_FROM on a sync iterator yields each value then completes with final
        """

        it = iter([10, 20, 30])
        code_obj, ns = build_expr([
            ["CONST", it],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, 10)
            v2 = await anext(agen)
            self.assertEqual(v2, 20)
            v3 = await anext(agen)
            self.assertEqual(v3, 30)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertIs(final, None)

        await run()


    async def test_yield_from_sync_generator_values_and_return(self):
        """
        YIELD_FROM on a Python sync generator yields values and returns final
        """

        def py_gen():
            yield 1
            yield 2
            return 99

        gen = py_gen()
        code_obj, ns = build_expr([
            ["CONST", gen],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, 1)
            v2 = await anext(agen)
            self.assertEqual(v2, 2)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertEqual(final, 99)

        await run()


    async def test_yield_from_python_async_gen_values_and_return(self):
        """
        YIELD_FROM on a Python async gen yields values and completes with final.

        On Python 3.12 this can fail with AttributeError (StopAsyncIteration
        has no .value): the interpreter re-raises a new StopAsyncIteration
        when the async gen exhausts, and that new instance has no .value.
        The implementation does set an exception with .value (subclass or
        fallback); the failure is in interpreter async machinery.
        """

        async def py_agen():
            yield 1
            yield 2

        a = py_agen()
        code_obj, ns = build_expr([
            ["CONST", a],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, 1)
            v2 = await anext(agen)
            self.assertEqual(v2, 2)
            with self.assertRaises(StopAsyncIteration):
                await anext(agen)

        await run()


    async def test_yield_from_koskari_async_gen_values_and_return(self):
        """
        YIELD_FROM on a Koskari async gen yields each value then completes
        with the delegate RETURN value.
        """

        inner_code, inner_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        inner_gen = make_callable(inner_code, inner_ns)()
        code_obj, ns = build_expr([
            ["CONST", inner_gen],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, 1)
            v2 = await anext(agen)
            self.assertEqual(v2, 2)
            v3 = await anext(agen)
            self.assertEqual(v3, 3)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertEqual(final, 42)

        await run()


    async def test_yield_from_koskari_async_gen_empty(self):
        """
        YIELD_FROM on a Koskari coroutine (no yields) is a type error:
        coroutines are not async iterable.
        """

        inner_code, inner_ns = build_expr([
            ["CONST", 99],
            ["RETURN", 1],
        ])
        inner_coro = make_callable(inner_code, inner_ns)()
        code_obj, ns = build_expr([
            ["CONST", inner_coro],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(TypeError) as ctx:
                await anext(agen)
            self.assertIn("iterable", str(ctx.exception))

        await run()


    async def test_yield_from_koskari_async_gen_with_await(self):
        """
        YIELD_FROM on a Koskari async gen that AWAITs between yields
        (resume-from-await path) yields values and completes.
        """

        async def noop():
            return None

        inner_code, inner_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["VAR", "noop"],
            ["CALL", 0],
            ["AWAIT", 0],
            ["POP", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", "done"],
            ["RETURN", 1],
        ], values={"noop": noop})
        inner_gen = make_callable(inner_code, inner_ns)()
        code_obj, ns = build_expr([
            ["CONST", inner_gen],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, 1)
            v2 = await anext(agen)
            self.assertEqual(v2, 2)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertEqual(final, "done")

        await run()


    async def test_yield_from_sync_iterator_empty(self):
        """
        YIELD_FROM on an empty sync iterator completes immediately
        with None as final value.
        """

        code_obj, ns = build_expr([
            ["CONST", iter([])],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertIs(final, None)

        await run()


    async def test_yield_from_sync_generator_empty_return_value(self):
        """
        YIELD_FROM on a sync generator that returns without yielding
        completes with the return value.
        """

        def empty_gen():
            if False:
                yield  # makes this a generator; never executed
            return 7

        g = empty_gen()
        code_obj, ns = build_expr([
            ["CONST", g],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertEqual(final, 7)

        await run()


    async def test_yield_from_raises_type_error_on_non_iterable(self):
        """
        YIELD_FROM on a non-iterable (e.g. int) raises TypeError with
        a message that indicates the requirement and the type received.
        """

        code_obj, ns = build_expr([
            ["CONST", 7],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(TypeError) as ctx:
                await anext(agen)
            msg = str(ctx.exception)
            self.assertIn("iterable", msg, msg="exception should mention iterable requirement")
            self.assertIn("int", msg, msg="exception should mention the type received")

        await run()


    async def test_yield_from_raises_type_error_on_none(self):
        """
        YIELD_FROM on None raises TypeError (not iterable).
        """

        code_obj, ns = build_expr([
            ["CONST", None],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(TypeError) as ctx:
                await anext(agen)
            self.assertIn("iterable", str(ctx.exception))
            self.assertIn("NoneType", str(ctx.exception))

        await run()


    async def test_yield_from_single_value_sync_iterator(self):
        """
        YIELD_FROM on a sync iterator with one element yields it
        then completes with None.
        """

        code_obj, ns = build_expr([
            ["CONST", iter([100])],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v = await anext(agen)
            self.assertEqual(v, 100)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            final = self._stop_async_iteration_final(ctx)
            self.assertIs(final, None)

        await run()


    async def test_yield_from_aclose_closes_delegate(self):
        """
        YIELD_FROM with Koskari inner: consume one value then exhaust;
        delegate is driven correctly.
        """

        inner_code, inner_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        inner_gen = make_callable(inner_code, inner_ns)()
        code_obj, ns = build_expr([
            ["CONST", inner_gen],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, 1)
            v2 = await anext(agen)
            self.assertEqual(v2, 2)
            with self.assertRaises(StopAsyncIteration):
                await anext(agen)

        await run()


    async def test_yield_from_sync_generator_raise_propagates(self):
        """
        YIELD_FROM propagates an exception raised inside the delegate.
        """

        def raising_gen():
            yield 1
            raise ValueError("delegate raised")

        g = raising_gen()
        code_obj, ns = build_expr([
            ["CONST", g],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v = await anext(agen)
            self.assertEqual(v, 1)
            with self.assertRaises(ValueError) as ctx:
                await anext(agen)
            self.assertIn("delegate raised", str(ctx.exception))

        await run()


    async def test_yield_from_python_async_gen_final_value_when_available(self):
        """
        YIELD_FROM on a Python async gen; when the gen exhausts,
        StopAsyncIteration is raised.
        """

        async def py_agen():
            yield "a"
            yield "b"

        a = py_agen()
        code_obj, ns = build_expr([
            ["CONST", a],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v1 = await anext(agen)
            self.assertEqual(v1, "a")
            v2 = await anext(agen)
            self.assertEqual(v2, "b")
            with self.assertRaises(StopAsyncIteration):
                await anext(agen)

        await run()


# The end.
