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
Tests for yield-from special and pairing with for-each.

The koslife-style reproducer compares iterating dict.items via a for-each
that yields each pair against a single yield-from over the same items view.
Runtime checks use compile_expr, await the outer expression to obtain the
lambda Function, then call it so the body runs with fast-local frames.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import sys

import pytest

from koskari.exceptions import SpecialError
from koskari.lib import OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


try:
    anext
except NameError:

    async def anext(agen):
        return await agen.__anext__()


def _opcodes(code):
    return [code.oparg(i)[0] for i in range(code.length())]


def _opcodes_with_arg(code):
    return [code.oparg(i) for i in range(code.length())]


async def _collect_async_pairs(agen):
    out = []
    while True:
        try:
            out.append(await anext(agen))
        except StopAsyncIteration:
            break
    return out


class TestCompileYieldFrom(SpecialsTestCase):

    async def test_yield_from_emits_opcode(self):
        code = self.compile_to_code("(yield-from (quote 1))")
        ops = _opcodes(code)
        self.assertIn(OpCode.YIELD_FROM, ops)
        yf = ops.index(OpCode.YIELD_FROM)
        ret = ops.index(OpCode.RETURN)
        self.assertLess(yf, ret)
        self.assertEqual(ops[yf - 1], OpCode.CONST)


    async def test_yield_from_arg_zero(self):
        code = self.compile_to_code("(yield-from (quote x))")
        pairs = _opcodes_with_arg(code)
        yf_idx = _opcodes(code).index(OpCode.YIELD_FROM)
        op, arg = pairs[yf_idx]
        self.assertEqual(op, OpCode.YIELD_FROM)
        self.assertEqual(arg, 0)


    async def test_yield_from_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(yield-from)")

        with self.assertRaises(SpecialError):
            self.compile_to_code("(yield-from a b)")


class TestYieldFromWithForEach(SpecialsTestCase):
    """
    Bytecode shape and runtime parity for dict.items-style patterns.
    """

    async def test_foreach_yield_body_has_no_yield_from(self):
        code = self.compile_to_code(
            """
            (lambda []
              (for-each [pair (call dict_items d)]
                (yield pair)))
            """,
            d={"u": 1, "v": 2},
            dict_items=lambda obj: obj.items(),
        )
        body = self.lambda_body_code(code)
        ops = _opcodes(body)
        self.assertIn(OpCode.PUSH_FOREACH, ops)
        self.assertIn(OpCode.YIELD, ops)
        self.assertNotIn(OpCode.YIELD_FROM, ops)


    async def test_yield_from_alone_has_foreach_loops(self):
        code = self.compile_to_code(
            """
            (lambda []
              (yield-from (call dict_items d)))
            """,
            d={"u": 1, "v": 2},
            dict_items=lambda obj: obj.items(),
        )
        body = self.lambda_body_code(code)
        ops = _opcodes(body)
        self.assertIn(OpCode.YIELD_FROM, ops)
        self.assertNotIn(OpCode.PUSH_FOREACH, ops)


    async def test_dict_items_foreach_yield_matches_yield_from(self):
        """
        Koslife reproducer: (for-each [item (data.items)] (yield item))
        vs (yield-from (data.items)) — same consumer-visible pairs.
        """

        data = {"a": 1, "b": 2, "c": 3}

        def dict_items(obj):
            return obj.items()

        src_foreach = """
        (lambda []
          (for-each [pair (call dict_items d)]
            (yield pair)))
        """
        src_yield_from = """
        (lambda []
          (yield-from (call dict_items d)))
        """

        defns = {"d": data, "dict_items": dict_items}
        expr_fe, _mod_fe = self.compile_expr(src_foreach, **defns)
        expr_yf, _mod_yf = self.compile_expr(src_yield_from, **defns)

        fn_fe = await expr_fe()
        fn_yf = await expr_yf()

        got_fe = await _collect_async_pairs(fn_fe())
        got_yf = await _collect_async_pairs(fn_yf())

        self.assertEqual(got_fe, got_yf)
        self.assertEqual(got_yf, list(data.items()))


class _StopsWithHeldInstance:
    """
    An empty Python async iterator whose first step raises a
    StopAsyncIteration instance the test holds a reference to
    """

    def __init__(self, stop):
        self.stop = stop

    def __aiter__(self):
        return self

    async def __anext__(self):
        raise self.stop


class _TwoStepAsyncIter:
    """
    A plain Python async iterator (no asend, athrow, or aclose) that
    yields two values and then stops
    """

    def __init__(self):
        self.items = ["one", "two"]

    def __aiter__(self):
        return self

    async def __anext__(self):
        if not self.items:
            raise StopAsyncIteration
        return self.items.pop(0)


class TestYieldFromPlainAsyncIterator(SpecialsTestCase):

    async def test_second_step_reaches_anext(self):
        """
        A consumer step after the first yielded value drives __anext__
        again instead of failing with no exception set
        """

        src = """
        (lambda []
          (yield-from (call source)))
        """
        expr, _mod = self.compile_expr(src, source=_TwoStepAsyncIter)
        fn = await expr()
        self.assertEqual(["one", "two"], await _collect_async_pairs(fn()))

    async def test_throw_lands_at_the_delegating_generator(self):
        """
        athrow into the delegating generator raises the thrown exception
        out of it, since a plain async iterator has no athrow
        """

        src = """
        (lambda []
          (yield-from (call source)))
        """
        expr, _mod = self.compile_expr(src, source=_TwoStepAsyncIter)
        fn = await expr()
        agen = fn()
        self.assertEqual("one", await anext(agen))
        with self.assertRaises(ValueError):
            await agen.athrow(ValueError("into the delegate"))


class TestYieldFromPythonAsyncExhaustion(SpecialsTestCase):

    async def test_stop_async_iteration_instance_released_once(self):
        """
        Exhausting a Python async iterable through yield-from releases
        the StopAsyncIteration instance exactly as many times as it was
        acquired; a held instance keeps its refcount
        """

        stop = StopAsyncIteration("held")
        keep = [stop] * 4

        src = """
        (lambda []
          (yield-from (call source)))
        """
        expr, _mod = self.compile_expr(
            src, source=lambda: _StopsWithHeldInstance(stop))
        fn = await expr()

        before = sys.getrefcount(stop)
        got = await _collect_async_pairs(fn())
        self.assertEqual([], got)

        # raising the instance gave it a traceback whose frame holds the
        # iterator, which holds the instance; drop that loop so only the
        # evaluator's own accounting is left to compare
        stop.__traceback__ = None
        self.assertEqual(before, sys.getrefcount(stop))
        del keep


# The end.
