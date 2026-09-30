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
Tests for VAR_STORE_CONST and FAST_STORE_CONST opcodes.

EXTRA schema (VAR, CONST, NOARG, NOARG) or (FASTVAR, CONST, NOARG, NOARG).
Stores a constant value without using the stack.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from koskari.lib import ModuleNamespace
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestVarStoreConstOpcodes(CevalTest):
    """
    Tests for VAR_STORE_CONST
    """

    async def test_var_store_const_updates_binding(self):
        """
        VAR_STORE_CONST stores constant into name; VAR loads it
        """
        code, ns = build_expr([
            ["VAR_STORE_CONST", ("x", 42, None, None)],
            ["VAR", "x"],
            ["RETURN", 1],
        ], local_syms=("x",), values={"x": 0})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


class TestFastStoreConstOpcodes(CevalTest):
    """
    Tests for FAST_STORE_CONST
    """

    async def test_fast_store_const_updates_slot(self):
        """
        FAST_STORE_CONST stores constant into fast slot; FAST loads it
        """
        code, ns = build_expr([
            ["FAST_STORE_CONST", (0, 100, None, None)],
            ["FAST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 100)


    async def test_fast_store_const_on_module_namespace_raises(self):
        """
        FAST_STORE_CONST code entered with a namespace without fast slots
        raises RuntimeError at eval entry
        """
        code_obj, _ = build_expr(
            [["FAST_STORE_CONST", (0, 1, None, None)], ["RETURN", 1]],
            local_syms=("x",))
        ns = ModuleNamespace("m", {})
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("fast slots", str(ctx.exception))


# The end.
