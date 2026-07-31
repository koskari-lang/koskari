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
Tests for compile/yield-from special and pairing with compile/for-each.

The koslife-style reproducer compares iterating dict.items via a for-each
that yields each pair against a single yield-from over the same items view.
Runtime checks use compile_expr, await the outer expression to obtain the
lambda Function, then call it so the body runs with fast-local frames.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from koskari.exceptions import SpecialError
from koskari.lib import OpCode

from . import SpecialsTestCase

import pytest

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
        code = self.compile_to_code("(compile/yield-from (compile/quote 1))")
        ops = _opcodes(code)
        self.assertIn(OpCode.YIELD_FROM, ops)
        yf = ops.index(OpCode.YIELD_FROM)
        ret = ops.index(OpCode.RETURN)
        self.assertLess(yf, ret)
        self.assertEqual(ops[yf - 1], OpCode.CONST)


    async def test_yield_from_arg_zero(self):
        code = self.compile_to_code("(compile/yield-from (compile/quote x))")
        pairs = _opcodes_with_arg(code)
        yf_idx = _opcodes(code).index(OpCode.YIELD_FROM)
        op, arg = pairs[yf_idx]
        self.assertEqual(op, OpCode.YIELD_FROM)
        self.assertEqual(arg, 0)


    async def test_yield_from_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/yield-from)")

        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/yield-from a b)")


class TestYieldFromWithForEach(SpecialsTestCase):
    """
    Bytecode shape and runtime parity for dict.items-style patterns.
    """

    async def test_foreach_yield_body_has_no_yield_from(self):
        code = self.compile_to_code(
            """
            (lambda []
              (compile/for-each [pair (compile/call dict_items d)]
                (compile/yield pair)))
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
              (compile/yield-from (compile/call dict_items d)))
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
          (compile/for-each [pair (compile/call dict_items d)]
            (compile/yield pair)))
        """
        src_yield_from = """
        (lambda []
          (compile/yield-from (compile/call dict_items d)))
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


# The end.
