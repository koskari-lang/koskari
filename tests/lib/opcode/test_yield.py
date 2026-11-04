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
Tests for YIELD and async gen opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import gc
import weakref

import pytest

from koskari.lib import AsyncGenerator, FormalsSpec, demand_call
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


pytestmark = pytest.mark.phase_lib


try:
    anext
except NameError:
    # anext wasn't available until Python 3.10
    async def anext(agen):
        return await agen.__anext__()


class TestYieldOpcodes(CevalTest):
    """
    Tests for YIELD opcode and async generators.
    """

    async def test_yielding_expression_returns_async_generator(self):
        """
        Calling an Expression whose code has YIELD returns AsyncGenerator
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(gen_code, gen_ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)


    async def test_async_generator_repr_includes_code_name(self):
        """
        AsyncGenerator repr matches Python-style <async_generator object name at ...>
        """
        gen_code, gen_ns = build_expr(
            [
                ["CONST", 1],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            name='my_ag',
        )
        expr = make_callable(gen_code, gen_ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)
        r = repr(agen)
        self.assertIn('async_generator object my_ag', r)
        self.assertIn(' at 0x', r)


    async def test_yield_suspends_and_resumes(self):
        """
        Yielding code returns AsyncGenerator; anext() yields value then StopAsyncIteration
        """

        code = [
            ["CONST", 42],
            ["YIELD", 1],
            ["RETURN", 0],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v = await anext(agen)
            self.assertEqual(v, 42)
            with self.assertRaises(StopAsyncIteration):
                await anext(agen)

        await run()

    async def test_asend_send_propagates_value(self):
        """
        asend.send(value) passes value into the generator.
        """
        code = [
            ["CONST", 1],
            ["YIELD", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        agen = expr()
        v1 = await anext(agen)
        self.assertEqual(v1, 1)
        asend = agen.__anext__()
        try:
            v2 = await asend.send(123)
        except StopIteration as e:
            v2 = e.value
        self.assertEqual(v2, 123)
        with self.assertRaises(StopAsyncIteration):
            await anext(agen)

    async def test_asend_throw_propagates_exception(self):
        """
        asend.throw(typ, val) injects exception into the generator.
        """
        code = [
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        agen = expr()
        v1 = await anext(agen)
        self.assertEqual(v1, 1)
        asend = agen.__anext__()
        with self.assertRaises(ValueError) as ctx:
            await asend.throw(ValueError, "thrown")
        self.assertIn("thrown", str(ctx.exception))

    async def test_asend_eval_error_propagates(self):
        """
        Gen that triggers TypeError on second step; documents current
        behavior (exception may be converted to StopAsyncIteration).
        """
        code = [
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 1],
            ["CONST", "bad"],
            ["OBJECT_ADD", 2],
            ["RETURN", 0],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        agen = expr()
        v1 = await anext(agen)
        self.assertEqual(v1, 1)
        asend = agen.__anext__()
        with self.assertRaises((TypeError, StopAsyncIteration)):
            await asend.send(None)


class TestYieldKoskariCallPath(CevalTest):
    """
    Tests for yielding functions invoked via Koskari CALL opcode.
    """

    def test_yielding_function_call_returns_async_generator(self):
        """
        Calling a yielding Function via CALL returns AsyncGenerator
        """
        _, base_ns = build_expr([["CONST", 0], ["RETURN", 1]], values={})
        yielding_code = build_code(
            [
                ["FAST", 0],
                ["YIELD", 1],
                ["POP", 1],
                ["FAST", 0],
                ["CONST", 1],
                ["OBJECT_ADD", 2],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            local_syms=("x",),
            name="yielding_fn",
        )
        yielding_fn = make_callable(yielding_code, base_ns, FormalsSpec(("x",)))
        caller_code, caller_ns = build_expr(
            [
                ["VAR", "foo"],
                ["CONST", 6],
                ["CALL", 1],
                ["RETURN", 1],
            ],
            values={"foo": yielding_fn},
        )
        caller = make_callable(caller_code, caller_ns)
        result = demand_call(caller)
        self.assertIsInstance(result, AsyncGenerator)
        self.assertEqual(list(result), [6, 7])


    def test_yielding_expression_call_returns_async_generator(self):
        """
        Calling a yielding Expression via CALL returns AsyncGenerator
        """
        gen_code, gen_ns = build_expr(
            [
                ["CONST", 42],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
        )
        yielding_expr = make_callable(gen_code, gen_ns)
        caller_code, caller_ns = build_expr(
            [
                ["VAR", "expr"],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            values={"expr": yielding_expr},
        )
        caller = make_callable(caller_code, caller_ns)
        result = demand_call(caller)
        self.assertIsInstance(result, AsyncGenerator)
        self.assertEqual(list(result), [42])


    def test_non_yielding_function_call_returns_value(self):
        """
        Calling a non-yielding Function via CALL returns computed value
        """
        _, base_ns = build_expr([["CONST", 0], ["RETURN", 1]], values={})
        normal_code = build_code(
            [
                ["FAST", 0],
                ["CONST", 41],
                ["OBJECT_ADD", 2],
                ["RETURN", 1],
            ],
            local_syms=("x",),
            name="normal_fn",
        )
        normal_fn = make_callable(normal_code, base_ns, FormalsSpec(("x",)))
        caller_code, caller_ns = build_expr(
            [
                ["VAR", "bar"],
                ["CONST", 1],
                ["CALL", 1],
                ["RETURN", 1],
            ],
            values={"bar": normal_fn},
        )
        caller = make_callable(caller_code, caller_ns)
        result = demand_call(caller)
        self.assertEqual(result, 42)


class TestKoskariIterKoskariGenerator(CevalTest):
    """
    Koskari PUSH_FOREACH/ITER_NEXT over a Koskari AsyncGenerator.

    PUSH_FOREACH takes the AsyncGenerator as a KosIter provider, and
    ITER_NEXT splices each step into the running chain; no sync adapter
    is involved.
    """

    async def test_koskari_foreach_over_koskari_generator(self):
        """
        Koskari consumer sums values yielded by a Koskari AsyncGenerator
        using PUSH_FOREACH / ITER_NEXT, exercising the implicit demand path.
        """
        inner_code, inner_ns = build_expr([
            ["CONST", 10],
            ["YIELD", 1],
            ["CONST", 20],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        inner_gen = make_callable(inner_code, inner_ns)()

        consumer_code, consumer_ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["VAR", "gen"],
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
        ], values={"gen": inner_gen, "total": 0})
        result = await make_callable(consumer_code, consumer_ns)()
        self.assertEqual(result, 30)


class TestAgenDeallocWhenFrozen(CevalTest):
    """
    Phase 8: Agen freed while suspended runs GeneratorExit teardown
    """

    async def test_drop_agen_at_yield_gc_no_crash(self):
        """
        Create agen, advance to first yield, drop all refs, gc collects.
        No crash; proper cleanup.
        """
        import gc

        code, ns = build_expr([
            ["CONST", 42],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)
        v = await anext(agen)
        self.assertEqual(v, 42)
        del agen
        gc.collect()

    async def test_agen_try_finally_runs_on_close(self):
        """
        Agen with try/finally in body; advance to yield; drop refs.
        Verify finally runs during GeneratorExit teardown, in the aclose
        the running loop's finalizer hook schedules for the generator.
        """
        import asyncio
        import gc

        ran = {}
        code, ns = build_expr([
            ["PUSH_FINALLY", "fin"],
            ["CONST", 1],
            ["YIELD", 1],
            ["LABEL", "fin"],
            ["VAR", "ran"],
            ["CONST", True],
            ["ITEM_CONST_STORE", "done"],
            ["EXCEPTION", 0],
            ["POP_FINALLY", 0],
            ["RAISE", 1],
        ], values={"ran": ran})
        expr = make_callable(code, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)
        v = await anext(agen)
        self.assertEqual(v, 1)
        self.assertNotIn("done", ran)
        del agen
        gc.collect()
        for _ in range(5):
            await asyncio.sleep(0)
        self.assertIn("done", ran)
        self.assertIs(ran["done"], True)


class Marker:
    """
    A weakly referenceable argument whose lifetime the tests observe
    """


class TestAgenReleasesFrameWhenFinished(CevalTest):
    """
    A generator driven from Python releases its frame, and every local
    with it, when it finishes rather than when the generator object dies
    """

    def make_generator_fn(self):
        """
        A yielding function of one argument that yields once, then returns
        """

        _, base_ns = build_expr([["CONST", 0], ["RETURN", 1]], values={})
        code = build_code(
            [
                ["CONST", 1],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            local_syms=("x",),
            name="yields_once",
        )
        return make_callable(code, base_ns, FormalsSpec(("x",)))


    async def test_exhausted_generator_releases_its_locals(self):
        """
        Exhausting the generator from Python frees its argument while
        the generator object is still held
        """

        fn = self.make_generator_fn()
        marker = Marker()
        ref = weakref.ref(marker)
        g = fn(marker)
        del marker
        self.assertIsInstance(g, AsyncGenerator)
        self.assertEqual([1], [v async for v in g])
        gc.collect()
        self.assertIsNone(ref())
        del g


    async def test_closed_generator_releases_its_locals(self):
        """
        Closing the generator from Python after its first element frees
        its argument while the generator object is still held
        """

        fn = self.make_generator_fn()
        marker = Marker()
        ref = weakref.ref(marker)
        g = fn(marker)
        del marker
        self.assertIsInstance(g, AsyncGenerator)
        self.assertEqual(1, await anext(g))
        self.assertIsNone(await g.aclose())
        gc.collect()
        self.assertIsNone(ref())
        del g


async def answer_once():
    yield 1


def context_of_step(agen):
    """
    The value and __context__ a sync step of agen's first __anext__
    carries on its StopIteration, taken inside a handled ValueError
    """

    try:
        raise ValueError("handled")
    except ValueError:
        step = agen.__anext__()
        try:
            step.__next__()
        except StopIteration as stop:
            return stop.value, stop.__context__
    return None


class TestAgenStopIterationChaining(CevalTest):
    """
    A Koskari generator stepped synchronously from Python carries its
    element in a StopIteration the way the running interpreter's own
    generator does, chaining a handled exception or not alike
    """

    def make_generator_fn(self):
        """
        A yielding function of one argument that yields it once
        """

        _, base_ns = build_expr([["CONST", 0], ["RETURN", 1]], values={})
        code = build_code(
            [
                ["FAST", 0],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            local_syms=("x",),
            name="yields_arg",
        )
        return make_callable(code, base_ns, FormalsSpec(("x",)))


    def test_chaining_matches_the_interpreter(self):
        """
        The Koskari generator's StopIteration has the __context__ the
        Python generator's has, None or the handled ValueError
        """

        want = context_of_step(answer_once())
        got = context_of_step(self.make_generator_fn()(1))
        self.assertEqual(want[0], got[0])
        self.assertIs(type(want[1]), type(got[1]))


# The end.
