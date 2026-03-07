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
Tests for FAST, FAST_BIND, FAST_CLEAR opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


class TestFastOpcodes(CevalTest):
    """
    Tests for FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE
    """

    async def _run_fast_out_of_bounds(self, opname, arg=0):
        """
        Run bytecode that hits the given FAST opcode with no fast slots
        """

        if opname == "FAST_STORE":
            code_list = [["CONST", 42], [opname, arg], ["RETURN", 1]]
        else:
            code_list = [[opname, arg], ["RETURN", 1]]
        code_obj, ns = build_expr(code_list)
        expr = make_callable(code_obj, ns)
        return await expr()


    async def test_fast_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            await self._run_fast_out_of_bounds("FAST", 0)


    async def test_fast_bind_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_BIND raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            await self._run_fast_out_of_bounds("FAST_BIND", 0)


    async def test_fast_clear_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_CLEAR raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            await self._run_fast_out_of_bounds("FAST_CLEAR", 0)


    async def test_fast_ref_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_REF raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            await self._run_fast_out_of_bounds("FAST_REF", 0)


    async def test_fast_store_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_STORE raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            await self._run_fast_out_of_bounds("FAST_STORE", 0)


    async def test_fast_on_dict_namespace_raises_runtime_error(self):
        """
        FAST requires KosNamespace with fast slots; dict as frame ns => RuntimeError
        """
        code_obj, _ = build_expr([["FAST", 0], ["RETURN", 1]])
        ns = {}
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("FAST invoked", str(ctx.exception))


    async def test_fast_bind_binds_slot(self):
        """
        FAST_BIND binds value to fast slot; subsequent FAST yields value
        """
        code, ns = build_expr([
            ["CONST", 100],
            ["FAST_STORE", 0],
            ["FAST_REF", 0],
            ["FAST_BIND", 1],
            ["FAST", 1],
            ["RETURN", 1],
        ], local_syms=("x", "y"))

        result = await make_callable(code, ns)()
        self.assertEqual(result, 100)


# The end.
