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
Tests for NEXT, ITER_NEXT opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import asyncio
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestNextOpcodes(CevalTest):
    """
    Tests for NEXT (stack-based).
    """

    def test_next_1_arg_pushes_value(self):
        """
        NEXT 1 on iterator pushes first value
        """
        code, ns = build_expr([
            ["CONST", [10]],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 10)


    def test_next_1_arg_stopiteration_raises(self):
        """
        NEXT 1 on exhausted iterator raises StopIteration (or wraps it)
        """
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        coro = make_callable(code, ns)()
        with self.assertRaises((StopIteration, RuntimeError)):
            while True:
                coro.send(None)


    def test_next_2_arg_pushes_value(self):
        """
        NEXT 2 on iterator pushes value; stack is [iterator, default]
        """
        code, ns = build_expr([
            ["CONST", [7]],
            ["OBJECT_ITER", 0],
            ["CONST", 0],
            ["NEXT", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 7)


    def test_next_2_arg_exhausted_returns_default(self):
        """
        NEXT 2 on exhausted iterator pushes default
        """
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_ITER", 0],
            ["CONST", 42],
            ["NEXT", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_next_on_iterable_calls_get_iter(self):
        """
        NEXT on iterable (not iterator) calls PyObject_GetIter; yields first value
        """
        code, ns = build_expr([
            ["CONST", [100, 200]],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 100)


    def test_next_arg_validation_rejects_invalid_args(self):
        """
        NEXT with arg other than 1 or 2 raises ValueError
        """
        for bad_arg in (0, 3):
            code, ns = build_expr([
                ["CONST", []],
                ["OBJECT_ITER", 0],
                ["NEXT", bad_arg],
                ["RETURN", 1],
            ])
            with self.assertRaises(ValueError) as cm:
                run_to_completion(make_callable(code, ns)())
            self.assertIn("NEXT arg must be 1 or 2", str(cm.exception))


    def test_next_on_non_iterable_raises_type_error(self):
        """
        NEXT on object that is neither iterable nor iterator raises TypeError
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            run_to_completion(make_callable(code, ns)())


    def test_next_accumulates_values_from_koskari_async_gen(self):
        """
        NEXT 1 on a Koskari async gen yields 1, 2, 3; consumer accumulates and returns 3
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v1"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v2"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v3"],
            ["VAR", "v3"],
            ["RETURN", 1],
        ], values={"gen": gen, "v1": None, "v2": None, "v3": None})
        result = run_to_completion(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 3)


    def test_next_with_await_in_generator(self):
        """
        Koskari async gen that AWAITs a Python async noop between yields; consumer still gets 1,2,3
        """
        async def noop():
            return None

        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["VAR", "noop"],
            ["CALL", 0],
            ["AWAIT", 0],
            ["POP", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ], values={"noop": noop})
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v1"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v2"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v3"],
            ["VAR", "v3"],
            ["RETURN", 1],
        ], values={"gen": gen, "v1": None, "v2": None, "v3": None})
        result = make_callable(consumer_code, consumer_ns)().run()
        self.assertEqual(result, 3)


    def test_foreach_iter_next_over_koskari_async_gen(self):
        """
        PUSH_FOREACH over a Koskari async gen; ITER_NEXT accumulates; last value is 3
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["CONST", None],
            ["VAR", "gen"],
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["VAR_STORE", "last"],
            ["VAR", "count"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "count"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["POP", 1],
            ["VAR", "last"],
            ["RETURN", 1],
        ], values={"gen": gen, "count": 0, "last": None})
        result = run_to_completion(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 3)


    def test_next_1_on_koskari_agen_exhausted_raises_stopiteration(self):
        """
        NEXT 1 on Koskari async gen after last value raises StopIteration
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["NEXT", 1],
            ["POP", 1],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["RETURN", 1],
        ], values={"gen": gen})
        coro = make_callable(consumer_code, consumer_ns)()
        with self.assertRaises((StopIteration, RuntimeError)):
            while True:
                coro.send(None)


    def test_next_2_on_koskari_agen_exhausted_returns_default(self):
        """
        NEXT 2 on Koskari async gen that returns immediately pushes default
        """
        gen_code, gen_ns = build_expr([
            ["RETURN", 0],
            ["CONST", None],
            ["YIELD", 1],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["CONST", 99],
            ["NEXT", 2],
            ["RETURN", 1],
        ], values={"gen": gen})
        result = run_to_completion(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 99)


    def test_next_on_python_async_iterator(self):
        """
        NEXT 1 on Python async iterator (async gen) yields values via GET_NEXT_SUSPEND
        """
        async def async_gen():
            yield 10
            yield 20
            yield 30

        async def run():
            aiter = async_gen()
            consumer_code, consumer_ns = build_expr([
                ["VAR", "aiter"],
                ["NEXT", 1],
                ["VAR_STORE", "v1"],
                ["VAR", "aiter"],
                ["NEXT", 1],
                ["VAR_STORE", "v2"],
                ["VAR", "aiter"],
                ["NEXT", 1],
                ["RETURN", 1],
            ], values={"aiter": aiter, "v1": None, "v2": None})
            return await make_callable(consumer_code, consumer_ns)()

        result = asyncio.run(run())
        self.assertEqual(result, 30)


    def test_next_2_on_python_async_iterator_exhausted_returns_default(self):
        """
        NEXT 2 on empty Python async iterator returns default
        """
        async def empty_async_gen():
            return
            yield

        async def run():
            aiter = empty_async_gen()
            consumer_code, consumer_ns = build_expr([
                ["VAR", "aiter"],
                ["CONST", 88],
                ["NEXT", 2],
                ["RETURN", 1],
            ], values={"aiter": aiter})
            return run_to_completion(make_callable(consumer_code, consumer_ns)())

        result = asyncio.run(run())
        self.assertEqual(result, 88)


    def test_next_2_on_koskari_agen_after_yields_then_exhausted_returns_default(self):
        """
        NEXT 2 on Koskari async gen after consuming values then exhausting returns default
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["CONST", -1],
            ["NEXT", 2],
            ["POP", 1],
            ["VAR", "gen"],
            ["CONST", -1],
            ["NEXT", 2],
            ["POP", 1],
            ["VAR", "gen"],
            ["CONST", 77],
            ["NEXT", 2],
            ["RETURN", 1],
        ], values={"gen": gen})
        result = run_to_completion(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 77)


    def test_next_2_on_python_async_iter_after_yields_then_exhausted_returns_default(self):
        """
        NEXT 2 on Python async iterator after consuming values then exhausting returns default
        """
        async def async_gen():
            yield 10
            yield 20

        async def run():
            aiter = async_gen()
            consumer_code, consumer_ns = build_expr([
                ["VAR", "aiter"],
                ["CONST", -1],
                ["NEXT", 2],
                ["POP", 1],
                ["VAR", "aiter"],
                ["CONST", -1],
                ["NEXT", 2],
                ["POP", 1],
                ["VAR", "aiter"],
                ["CONST", 66],
                ["NEXT", 2],
                ["RETURN", 1],
            ], values={"aiter": aiter})
            return run_to_completion(make_callable(consumer_code, consumer_ns)())

        result = asyncio.run(run())
        self.assertEqual(result, 66)


    def test_next_on_iterator_that_raises_propagates_error(self):
        """
        NEXT on iterator whose __next__ raises (not StopIteration) propagates GET_NEXT_ERROR
        """
        class BadIterator(object):
            def __iter__(self):
                return self

            def __next__(self):
                raise ValueError("iterator raised")

        code, ns = build_expr([
            ["CONST", BadIterator()],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(ValueError) as cm:
            run_to_completion(make_callable(code, ns)())
        self.assertIn("iterator raised", str(cm.exception))


# The end.
