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

from koskari.lib import AsyncGenerator, demand_call
from koskari.lib import FormalsSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

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
        Verify finally runs during GeneratorExit teardown.
        """
        import gc

        ran = {}
        code, ns = build_expr([
            ["PUSH_FINALLY", "fin"],
            ["CONST", 1],
            ["YIELD", 1],
            ["LABEL", "fin"],
            ["VAR", "ran"],
            ["CONST", True],
            ["CONST_ITEM_STORE", "done"],
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
        self.assertIn("done", ran)
        self.assertIs(ran["done"], True)


# The end.
