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
Tests for PUSH_FOREACH, ITER_NEXT opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import load_module_source_async, new_module
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestForeachOpcodes(CevalTest):
    """
    Tests for PUSH_FOREACH with ITER_NEXT.

    PUSH_FOREACH expects stack: [iterable].
    It pops the iterable, creates an iterator, and stores it in the region.
    ITER_NEXT reads from region. POP_FOREACH pushes region->result, or None
    when no BREAK/CONTINUE set a result.
    """

    async def test_iter_next_outside_foreach_fails(self):
        """
        ITER_NEXT outside PUSH_FOREACH fails at Code construction or runtime
        """

        try:
            code, ns = build_expr([
                ["CONST", [10]],
                ["ITER_NEXT", "else_lbl"],
                ["RETURN", 1],
                ["LABEL", "else_lbl"],
                ["CONST", 0],
                ["RETURN", 1],
            ])
        except RuntimeError as e:
            self.assertIn("ITER_NEXT", str(e))
            self.assertIn("PUSH_FOREACH", str(e))
            return
        with self.assertRaises(RuntimeError) as cm:
            await make_callable(code, ns)()
        self.assertIn("ITER_NEXT", str(cm.exception))
        self.assertIn("PUSH_FOREACH", str(cm.exception))


    async def test_foreach_iterates_list(self):
        """
        FOREACH over list returns first element
        """

        code, ns = build_expr([
            ["CONST", [10, 20, 30]],  # iterable
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["LABEL", "after_pop"],
            # POP_FOREACH pushed the loop result; we want to return -1
            ["POP", 1],
            ["CONST", -1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)


    async def test_foreach_continue_steps_iterator(self):
        """
        CONTINUE with the FOREACH region on top returns to the step,
        which lands on the else target at exhaustion
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["CONST", [1, 2, 3, 4]],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["VAR", "total"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["CONTINUE", 0],
            ["LABEL", "else_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "total"],
            ["RETURN", 1],
        ], values={"total": 0})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)


    async def test_foreach_continue_value_is_loop_result(self):
        """
        CONTINUE with a value records it as the region result, which
        POP_FOREACH returns when the loop runs out
        """

        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONTINUE", 1],
            ["LABEL", "else_lbl"],
            ["POP_FOREACH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 3)


    async def test_foreach_break_exits_early(self):
        """
        FOREACH BREAK exits when value equals 2. BREAK with arg=0 uses None
        as the result value.
        """

        code, ns = build_expr([
            ["CONST", [1, 2, 3, 4]],  # iterable
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", 2],
            ["OBJECT_EQ", 2],
            ["JUMP_FALSE", "loop_cont"],
            ["BREAK", 0],
            ["LABEL", "loop_cont"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["LABEL", "after_pop"],
            # POP_FOREACH pushed the loop result (None from break)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_foreach_empty_exits_to_else(self):
        """
        FOREACH over empty list exits to else target
        """

        code, ns = build_expr([
            ["CONST", []],    # iterable
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["LABEL", "after_pop"],
            # POP_FOREACH pushed the loop result (None)
            ["POP", 1],
            ["CONST", 99],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_push_foreach_with_iter_next_completes(self):
        """
        PUSH_FOREACH with ITER_NEXT on empty list exits to else.
        POP_FOREACH pushes the loop result (None in this case).
        """

        code, ns = build_expr([
            ["CONST", []],    # iterable
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            # POP_FOREACH now pushes the loop result (None)
            ["POP", 1],  # discard the loop result
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_foreach_iter_next_on_python_async_iterator(self):
        """
        PUSH_FOREACH over Python async iterator; ITER_NEXT yields values via GET_NEXT_SUSPEND
        """
        async def async_gen():
            yield 5
            yield 15
            yield 25

        async def run():
            aiter = async_gen()
            consumer_code, consumer_ns = build_expr([
                ["VAR", "aiter"],
                ["PUSH_FOREACH", 0],
                ["LABEL", "loop_head"],
                ["ITER_NEXT", "else_lbl"],
                ["VAR_STORE", "last"],
                ["JUMP", "loop_head"],
                ["LABEL", "else_lbl"],
                ["LABEL", "break_lbl"],
                ["POP_FOREACH", 0],
                ["POP", 1],
                ["VAR", "last"],
                ["RETURN", 1],
            ], values={"aiter": aiter, "last": None})
            return await make_callable(consumer_code, consumer_ns)()

        result = await run()
        self.assertEqual(result, 25)


    async def test_foreach_iter_next_iterates_all_values(self):
        """
        PUSH_FOREACH with ITER_NEXT over list accumulates; else runs when exhausted
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["CONST", [1, 2, 3, 4, 5]],
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["VAR", "total"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "total"],
            ["RETURN", 1],
        ], values={"total": 0})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 15)


    async def test_foreach_iter_next_on_empty_async_iterator_exits_to_else(self):
        """
        PUSH_FOREACH over empty Python async iterator exits to else
        """

        async def empty_gen():
            if False:
                yield

        async def run():
            aiter = empty_gen()
            consumer_code, consumer_ns = build_expr([
                ["VAR", "aiter"],
                ["PUSH_FOREACH", 0],
                ["ITER_NEXT", "else_lbl"],
                ["CONST", -1],
                ["RETURN", 1],
                ["LABEL", "else_lbl"],
                ["LABEL", "break_lbl"],
                ["POP_FOREACH", 0],
                ["POP", 1],
                ["CONST", 999],
                ["RETURN", 1],
            ], values={"aiter": aiter})
            return await make_callable(consumer_code, consumer_ns)()

        result = await run()
        self.assertEqual(result, 999)


    async def test_foreach_with_non_iterable_raises_type_error(self):
        """
        PUSH_FOREACH over non-iterable (e.g. int) raises TypeError
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


    async def test_foreach_iter_next_when_iterator_raises_propagates_error(self):
        """
        ITER_NEXT when iterator raises on __next__ propagates GET_NEXT_ERROR
        """

        class BadIterator(object):
            def __iter__(self):
                return self

            def __next__(self):
                raise ValueError("iterator raised in foreach")

        code, ns = build_expr([
            ["CONST", BadIterator()],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(ValueError) as cm:
            await make_callable(code, ns)()
        self.assertIn("iterator raised in foreach", str(cm.exception))


    async def test_foreach_stop_iteration_with_value_is_exhaustion(self):
        """
        A sync iterator raising StopIteration(value) from __next__ ends
        the loop through the else branch; the value is not an error
        """

        class Counted(object):
            def __init__(self):
                self.n = 0

            def __iter__(self):
                return self

            def __next__(self):
                self.n += 1
                if self.n > 2:
                    raise StopIteration("done")
                return self.n

        code, ns = build_expr([
            ["CONST", Counted()],
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["VAR_STORE", "last"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "last"],
            ["RETURN", 1],
        ], values={"last": None})
        self.assertEqual(await make_callable(code, ns)(), 2)


    async def test_foreach_prefers_anext_over_next(self):
        """
        An iterator offering both __anext__ and __next__ is stepped as an
        async iterator; __next__ is never called
        """

        class Both(object):
            def __init__(self):
                self.n = 0

            def __aiter__(self):
                return self

            async def __anext__(self):
                self.n += 1
                if self.n > 3:
                    raise StopAsyncIteration
                return self.n * 10

            def __iter__(self):
                return self

            def __next__(self):
                raise AssertionError("__next__ must not be stepped")

        code, ns = build_expr([
            ["CONST", Both()],
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["VAR_STORE", "last"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "last"],
            ["RETURN", 1],
        ], values={"last": None})
        self.assertEqual(await make_callable(code, ns)(), 30)


class TestIterNextStoreOpcodes(CevalTest):
    """
    ITER_NEXT_STORE_FAST and ITER_NEXT_STORE_VAR step the region
    iterator and store the element instead of pushing it
    """

    async def test_store_fast_sums_a_list(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["FAST_STORE", 1],
            ["CONST", [1, 2, 3, 4]],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT_STORE_FAST", (0, "else_lbl", None, None)],
            ["FAST", 1],
            ["FAST", 0],
            ["OBJECT_ADD", 2],
            ["FAST_STORE", 1],
            ["CONTINUE", 0],
            ["LABEL", "else_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["FAST", 1],
            ["RETURN", 1],
        ], local_syms=("x", "total"))
        self.assertEqual(await make_callable(code, ns)(), 10)


    async def test_store_fast_leaves_the_stack_alone(self):
        code, ns = build_expr([
            ["CONST", "below"],
            ["CONST", [7]],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT_STORE_FAST", (0, "else_lbl", None, None)],
            ["BREAK", 0],
            ["LABEL", "else_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["FAST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        self.assertEqual(await make_callable(code, ns)(), 7)


    async def test_store_var_stores_by_name(self):
        code, ns = build_expr([
            ["CONST", [5, 6]],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT_STORE_VAR", ("x", "else_lbl", None, None)],
            ["BREAK", 0],
            ["LABEL", "else_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": None})
        self.assertEqual(await make_callable(code, ns)(), 5)


    async def test_store_fast_outside_foreach_fails(self):
        try:
            code, ns = build_expr([
                ["CONST", [10]],
                ["ITER_NEXT_STORE_FAST", (0, "else_lbl", None, None)],
                ["RETURN", 0],
                ["LABEL", "else_lbl"],
                ["RETURN", 0],
            ], local_syms=("x",))
        except RuntimeError as e:
            self.assertIn("ITER_NEXT", str(e))
            self.assertIn("PUSH_FOREACH", str(e))
            return
        with self.assertRaises(RuntimeError) as cm:
            await make_callable(code, ns)()
        self.assertIn("PUSH_FOREACH", str(cm.exception))


FUSED_SRC = """
(define [total items]
  (define acc 0)
  (for-each [x items]
    (set! acc (+ acc x)))
  acc)

(define [gen n]
  (define i 0)
  (while (< i n)
    (yield i)
    (set! i (+ i 1))))

(define [total-gen n]
  (define acc 0)
  (for-each [x (gen n)]
    (set! acc (+ acc x)))
  acc)

(define [total-second it]
  (define acc 0)
  (for-each [x it]
    (set! acc (+ acc (item x 1))))
  acc)

(define [collect-k choices]
  (reset
    (+ 100 (shift k
             (let [[out (#list)]]
               (for-each [c choices]
                 (out.append (call k c)))
               out)))))

(define module-acc 0)
(for-each [x #[1 2 3]]
  (set! module-acc (+ module-acc x)))
"""


class AsyncCounter:
    def __init__(self, n):
        self.i = 0
        self.n = n

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        return ("async", self.i)


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class TestIterNextStoreFromSource(unittest.IsolatedAsyncioTestCase):
    """
    A for-each binding compiles to the fused step, and the element lands
    in the slot on every delivery path: a sync iterator, a Koskari
    generator, a Python async iterator, a continuation resumed inside
    the loop, and a module-level loop with no fast slot
    """

    async def load(self):
        module = new_module("foreach_store_fused")
        vars(module)["__file__"] = "<foreach_store_fused>"
        await load_module_source_async(module, FUSED_SRC, layout=False)
        return vars(module)

    async def test_binding_compiles_to_the_fused_step(self):
        mod = await self.load()
        ops = opcodes_of(mod["total"])
        self.assertIn(OpCode.ITER_NEXT_STORE_FAST, ops)
        self.assertNotIn(OpCode.ITER_NEXT, ops)
        self.assertNotIn(OpCode.FAST_STORE, ops[ops.index(OpCode.PUSH_FOREACH):])

    async def test_sync_iterator(self):
        mod = await self.load()
        self.assertEqual(await mod["total"]([1, 2, 3, 4]), 10)

    async def test_koskari_generator(self):
        mod = await self.load()
        self.assertEqual(await mod["total-gen"](5), 10)

    async def test_python_async_iterator(self):
        mod = await self.load()
        self.assertEqual(await mod["total-second"](AsyncCounter(4)), 10)

    async def test_continuation_resumed_inside_the_loop(self):
        mod = await self.load()
        self.assertEqual(await mod["collect-k"]([1, 2, 3]), [101, 102, 103])

    async def test_module_level_loop(self):
        mod = await self.load()
        self.assertEqual(mod["module-acc"], 6)

    async def test_store_step_away_from_the_continue_target(self):
        """
        A store step that defers resumes through the instruction that
        deferred, not through the region's continue target: here the
        continue target is a JUMP to the step, over a Koskari generator
        so every step installs and resumes
        """

        mod = await self.load()
        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["VAR", "gen"],
            ["CONST", 4],
            ["CALL", 1],
            ["PUSH_FOREACH", 0],
            ["JUMP", "step"],
            ["LABEL", "body"],
            ["VAR", "total"],
            ["VAR", "x"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["CONTINUE", 0],
            ["LABEL", "step"],
            ["ITER_NEXT_STORE_VAR", ("x", "else_lbl", None, None)],
            ["JUMP", "body"],
            ["LABEL", "else_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "total"],
            ["RETURN", 1],
        ], values={"gen": mod["gen"], "total": 0, "x": None})
        self.assertEqual(await make_callable(code, ns)(), 6)


# The end.
