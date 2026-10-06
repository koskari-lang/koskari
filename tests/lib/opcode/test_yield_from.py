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

import pytest

from koskari.lib import AsyncGenerator, FormalsSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


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
            ["RETURN", 0],
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
            ["RETURN", 0],
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
            self.assertIs(self._stop_async_iteration_final(ctx), None)

        await run()


    async def test_yield_from_sync_iterator_values_and_final(self):
        """
        YIELD_FROM on a sync iterator yields each value then completes with final
        """

        it = iter([10, 20, 30])
        code_obj, ns = build_expr([
            ["CONST", it],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
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
        YIELD_FROM on a Python sync generator yields values; sync generator
        return value is not surfaced on the hosting stack or StopAsyncIteration.
        """

        def py_gen():
            yield 1
            yield 2
            return 99

        gen = py_gen()
        code_obj, ns = build_expr([
            ["CONST", gen],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
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
            self.assertIs(final, None)

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
            ["RETURN", 0],
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


    async def test_yield_from_python_async_iterator_values(self):
        """
        YIELD_FROM on a plain Python async iterator (no asend) yields its
        values and completes
        """

        class Counter(object):
            def __init__(self):
                self.n = 0

            def __aiter__(self):
                return self

            async def __anext__(self):
                self.n += 1
                if self.n > 2:
                    raise StopAsyncIteration
                return self.n * 10

        code_obj, ns = build_expr([
            ["CONST", Counter()],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        agen = make_callable(code_obj, ns)()
        self.assertIsInstance(agen, AsyncGenerator)
        self.assertEqual(await anext(agen), 10)
        self.assertEqual(await anext(agen), 20)
        with self.assertRaises(StopAsyncIteration):
            await anext(agen)


    async def test_yield_from_python_async_gen_receives_sent_values(self):
        """
        YIELD_FROM on a Python async gen forwards a sent value through
        asend to the delegate
        """

        async def py_agen():
            got = yield 1
            yield ("got", got)

        code_obj, ns = build_expr([
            ["CONST", py_agen()],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        agen = make_callable(code_obj, ns)()
        self.assertEqual(await anext(agen), 1)
        self.assertEqual(await agen.asend("hello"), ("got", "hello"))
        with self.assertRaises(StopAsyncIteration):
            await anext(agen)


    async def test_yield_from_koskari_async_gen_values_and_return(self):
        """
        YIELD_FROM on a Koskari async gen yields each value; inner RETURN
        payload is not left on the hosting stack (not surfaced as SAI .value).
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
            ["RETURN", 0],
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
            self.assertIs(final, None)

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
            ["RETURN", 0],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(TypeError) as ctx:
                await anext(agen)
            self.assertIn("iterable", str(ctx.exception))

        await run()


    async def test_yield_from_defer_rejected(self):
        """
        YIELD_FROM on a defer is a type error: like coroutines, defers
        are awaitable handles, not iterables (tp_iternext exists only
        for the await protocol).
        """

        from koskari.lib import defer

        async def work():
            return 99

        handle = defer(work)
        code_obj, ns = build_expr([
            ["CONST", handle],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(TypeError) as ctx:
                await anext(agen)
            self.assertIn("iterable", str(ctx.exception))

        await run()
        handle.close()


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
            ["AWAIT", 1],
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
            ["RETURN", 0],
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
            self.assertIs(self._stop_async_iteration_final(ctx), None)

        await run()


    async def test_yield_from_sync_iterator_empty(self):
        """
        YIELD_FROM on an empty sync iterator completes immediately
        with None as final value.
        """

        code_obj, ns = build_expr([
            ["CONST", []],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
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


    async def test_yield_from_dict_obtains_iterator_from_tos(self):
        """
        YIELD_FROM must call iter() on the TOS delegate; a dict on the stack
        is not pre-wrapped as an iterator in the test harness.
        """

        d = {"first": 1, "second": 2, "third": 3}
        code_obj, ns = build_expr([
            ["CONST", d],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            keys = []
            for _ in range(3):
                keys.append(await anext(agen))
            self.assertEqual(keys, ["first", "second", "third"])
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            self.assertIs(self._stop_async_iteration_final(ctx), None)

        await run()


    async def test_yield_from_set_obtains_iterator_from_tos(self):
        """
        YIELD_FROM obtains an iterator from a set on TOS; iteration order is
        undefined, so compare the multiset of yielded values.
        """

        s = {11, 22, 33}
        code_obj, ns = build_expr([
            ["CONST", s],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            got = {await anext(agen), await anext(agen), await anext(agen)}
            self.assertEqual(got, s)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            self.assertIs(self._stop_async_iteration_final(ctx), None)

        await run()


    async def test_yield_from_range_obtains_iterator_from_tos(self):
        """
        YIELD_FROM must iterate a range object placed on TOS without the test
        calling iter() first.
        """

        r = range(5, 8)
        code_obj, ns = build_expr([
            ["CONST", r],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            self.assertEqual(await anext(agen), 5)
            self.assertEqual(await anext(agen), 6)
            self.assertEqual(await anext(agen), 7)
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            self.assertIs(self._stop_async_iteration_final(ctx), None)

        await run()


    async def test_yield_from_sync_generator_empty_return_value(self):
        """
        YIELD_FROM on a sync generator that returns without yielding
        completes without surfacing the generator return value on the
        hosting frame (StopAsyncIteration final is None).
        """

        def empty_gen():
            if False:
                yield  # makes this a generator; never executed
            return 7

        g = empty_gen()
        code_obj, ns = build_expr([
            ["CONST", g],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            with self.assertRaises(StopAsyncIteration) as ctx:
                await anext(agen)
            self.assertIs(self._stop_async_iteration_final(ctx), None)

        await run()


    async def test_yield_from_completion_then_return_underrun(self):
        """
        After YIELD_FROM completes on an exhausted delegate, the hosting
        frame stack must not hold a value for RETURN 1 to pop; doing so
        must raise stack underrun.
        """

        code_obj, ns = build_expr([
            ["CONST", iter(())],
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()

        async def run():
            with self.assertRaises(RuntimeError) as ctx:
                await anext(agen)
            self.assertIn("stack underrun", str(ctx.exception).lower())

        await run()


    async def test_yield_from_raises_type_error_on_non_iterable(self):
        """
        YIELD_FROM on a non-iterable (e.g. int) raises TypeError with
        a message that indicates the requirement and the type received.
        """

        code_obj, ns = build_expr([
            ["CONST", 7],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
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
            ["RETURN", 0],
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
            ["RETURN", 0],
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
            ["RETURN", 0],
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
            ["RETURN", 0],
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
            ["RETURN", 0],
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


    async def test_yield_from_inside_kos_function_outer_foreach_sums(self):
        """
        Inner Koskari Function body uses YIELD_FROM; an outer Koskari frame
        CALLs that function, then PUSH_FOREACH / ITER_NEXT / CONTINUE sums
        values from the returned async generator.
        """

        inner = build_code([
            ["CONST", [7, 8, 9]],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ], name="test_gen")
        fspec = inner.function_spec(FormalsSpec())
        factory_code, factory_ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", ((), (), 0, 0)],
            ["RETURN", 1],
        ], name="make_test_gen")
        test_gen = await make_callable(factory_code, factory_ns)()

        consumer_code, consumer_ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["VAR", "test_gen"],
            ["CALL", 0],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["VAR", "total"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["CONTINUE", 0],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "total"],
            ["RETURN", 1],
        ], values={"test_gen": test_gen, "total": 0})
        result = await make_callable(consumer_code, consumer_ns)()
        self.assertEqual(result, 24)


    async def test_yield_from_range_inside_kos_function_outer_foreach(self):
        """
        Same as test_yield_from_inside_kos_function_outer_foreach_sums but the
        inner function YIELD_FROMs a range on TOS (no iter() in the test
        harness).
        """

        inner = build_code([
            ["CONST", range(2, 5)],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ], name="test_gen")
        fspec = inner.function_spec(FormalsSpec())
        factory_code, factory_ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", ((), (), 0, 0)],
            ["RETURN", 1],
        ], name="make_test_gen")
        test_gen = await make_callable(factory_code, factory_ns)()

        consumer_code, consumer_ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["VAR", "test_gen"],
            ["CALL", 0],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["VAR", "total"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["CONTINUE", 0],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "total"],
            ["RETURN", 1],
        ], values={"test_gen": test_gen, "total": 0})
        result = await make_callable(consumer_code, consumer_ns)()
        self.assertEqual(result, 9)



class TestYieldFromKosIterProvider(CevalTest):
    """
    YIELD_FROM over a KosIter provider that steps in place instead of
    grafting frames (KosMap): elements re-emit as the delegating
    generator's own yields, true suspensions park PC-stable, and
    exhaustion completes the delegation
    """

    def make_yf_agen(self, delegate):
        code_obj, ns = build_expr([
            ["CONST", delegate],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        agen = make_callable(code_obj, ns)()
        self.assertIsInstance(agen, AsyncGenerator)
        return agen

    async def test_yield_from_map_sync_elements(self):
        """
        map over a sync source with a sync function delegates its
        elements through yield-from
        """

        from koskari.lib import KosMap

        agen = self.make_yf_agen(KosMap(abs, [-1, -2, -3]))
        self.assertEqual(await anext(agen), 1)
        self.assertEqual(await anext(agen), 2)
        self.assertEqual(await anext(agen), 3)
        with self.assertRaises(StopAsyncIteration):
            await anext(agen)

    async def test_yield_from_map_parked_step(self):
        """
        map over an async function suspends the yield-from step on the
        function's awaitable and resumes with its result
        """

        import asyncio

        from koskari.lib import KosMap

        async def ainc(x):
            await asyncio.sleep(0)
            return x + 1

        agen = self.make_yf_agen(KosMap(ainc, [10, 20]))
        self.assertEqual(await anext(agen), 11)
        self.assertEqual(await anext(agen), 21)
        with self.assertRaises(StopAsyncIteration):
            await anext(agen)

    async def test_yield_from_map_exhausted_delegate(self):
        """
        an exhausted map completes the delegation immediately
        """

        from koskari.lib import KosMap

        exhausted = KosMap(abs, [])
        agen = self.make_yf_agen(exhausted)
        with self.assertRaises(StopAsyncIteration):
            await anext(agen)

    async def test_yield_from_map_demand_drive(self):
        """
        the demand path iterates a yield-from over a map with a sync
        function; elements arrive synchronously
        """

        from koskari.lib import KosMap

        agen = self.make_yf_agen(KosMap(str.upper, ["a", "b"]))
        self.assertEqual(list(iter(agen)), ["A", "B"])


# The end.
