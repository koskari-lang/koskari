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

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestForeachOpcodes(CevalTest):
    """
    Tests for PUSH_FOREACH with ITER_NEXT.

    PUSH_FOREACH expects stack: [loop_result, iterable].
    It pops both, creates an iterator from the iterable, and stores
    loop_result and iterator in the region. ITER_NEXT reads from region.
    POP_FOREACH pushes region->result to the stack as the loop expression value.
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
            ["CONST", None],  # loop_result
            ["CONST", [10, 20, 30]],  # iterable
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_FOREACH", 0],
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


    async def test_foreach_break_exits_early(self):
        """
        FOREACH BREAK exits when value equals 2. BREAK with arg=0 uses None
        as the result value.
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
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
            ["CLEAR_FOREACH", 0],
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
            ["CONST", None],  # loop_result
            ["CONST", []],    # iterable
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_FOREACH", 0],
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
            ["CONST", None],  # loop_result
            ["CONST", []],    # iterable
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_FOREACH", 0],
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
                ["CONST", None],
                ["VAR", "aiter"],
                ["PUSH_FOREACH", 0],
                ["LABEL", "loop_head"],
                ["ITER_NEXT", "else_lbl"],
                ["VAR_STORE", "last"],
                ["JUMP", "loop_head"],
                ["LABEL", "else_lbl"],
                ["CLEAR_FOREACH", 0],
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
            ["CONST", None],
            ["CONST", [1, 2, 3, 4, 5]],
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["VAR", "total"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["CLEAR_FOREACH", 0],
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
                ["CONST", None],
                ["VAR", "aiter"],
                ["PUSH_FOREACH", 0],
                ["ITER_NEXT", "else_lbl"],
                ["CONST", -1],
                ["RETURN", 1],
                ["LABEL", "else_lbl"],
                ["CLEAR_FOREACH", 0],
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
            ["CONST", None],
            ["CONST", 42],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_FOREACH", 0],
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
            ["CONST", None],
            ["CONST", BadIterator()],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_FOREACH", 0],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(ValueError) as cm:
            await make_callable(code, ns)()
        self.assertIn("iterator raised in foreach", str(cm.exception))


# The end.
