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
Tests for the control rows that carry their value on the row:
RETURN_CONST, RETURN_VAR, RETURN_FAST and YIELD_CONST, YIELD_VAR,
YIELD_FAST. Each delivers exactly as the single-value form of RETURN or
YIELD does, with the value taken from the constant pool, a name in the
frame namespace, or a fast slot instead of the stack.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import FormalsSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


pytestmark = pytest.mark.phase_lib


def fast_fn(instr, syms):
    """
    A function over the fast locals syms running instr
    """

    code = build_code(instr, local_syms=syms, name="fn")
    base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
    return make_callable(code, base_ns, FormalsSpec(syms))


async def collect(agen):
    out = []
    async for v in agen:
        out.append(v)
    return out


class TestReturnOperand(CevalTest):

    async def test_return_const(self):
        code, ns = build_expr([["RETURN_CONST", 42]])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_return_const_leaves_stack_alone(self):
        code, ns = build_expr([
            ["CONST", "below"],
            ["RETURN_CONST", 42],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_return_var(self):
        code, ns = build_expr([["RETURN_VAR", "n"]], values={"n": 7})
        self.assertEqual(await make_callable(code, ns)(), 7)


    async def test_return_var_unbound_raises(self):
        code, ns = build_expr([["RETURN_VAR", "missing"]])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_return_fast(self):
        fn = fast_fn([["RETURN_FAST", 0]], ("a",))
        self.assertEqual(await fn(5), 5)
        self.assertEqual(await fn("x"), "x")


    async def test_return_var_from_call(self):
        """
        The value delivers to a bytecode caller through the plain return
        """

        inner_code, inner_ns = build_expr(
            [["RETURN_VAR", "n"]], values={"n": 11})
        inner = make_callable(inner_code, inner_ns)
        code, ns = build_expr([
            ["VAR", "inner"],
            ["CALL", 0],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ], values={"inner": inner})
        self.assertEqual(await make_callable(code, ns)(), 12)


class TestYieldOperand(CevalTest):

    async def test_yield_const(self):
        code, ns = build_expr([
            ["YIELD_CONST", 1],
            ["YIELD_CONST", 2],
            ["RETURN", 0],
        ])
        self.assertTrue(code.yields)
        self.assertEqual(await collect(make_callable(code, ns)()), [1, 2])


    async def test_yield_var(self):
        code, ns = build_expr([
            ["YIELD_VAR", "x"],
            ["YIELD_VAR", "y"],
            ["RETURN", 0],
        ], values={"x": "a", "y": "b"})
        self.assertTrue(code.yields)
        self.assertEqual(
            await collect(make_callable(code, ns)()), ["a", "b"])


    async def test_yield_var_unbound_raises(self):
        code, ns = build_expr([
            ["YIELD_VAR", "missing"],
            ["RETURN", 0],
        ])
        with self.assertRaises(NameError):
            await collect(make_callable(code, ns)())


    async def test_yield_fast(self):
        fn = fast_fn([
            ["YIELD_FAST", 0],
            ["YIELD_FAST", 1],
            ["RETURN", 0],
        ], ("a", "b"))
        self.assertEqual(await collect(fn(3, 4)), [3, 4])


# The end.
