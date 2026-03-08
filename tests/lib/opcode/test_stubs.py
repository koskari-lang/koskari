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
Tests for stub opcodes (no runtime implementation yet).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


class TestPushCollectPopCollectBuildClass(CevalTest):

    async def test_push_collect_pop_collect_build_class_defines_class(self):
        """
        PUSH_COLLECT / POP_COLLECT / BUILD_CLASS define a class via bytecode
        """

        from koskari.lib import NamespaceSpec

        collect_nspec = NamespaceSpec("C", (), ())
        code_obj, ns = build_expr([
            ["CONST", "C"],
            ["CONST", ()],
            ["BUILD_ARGS", 2],
            ["PUSH_COLLECT", collect_nspec],
            ["POP_COLLECT", 0],
            ["BUILD_CLASS", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertIsInstance(result, type)
        self.assertEqual(result.__name__, "C")
        KosType = __import__("koskari.lib", fromlist=["KosType"]).KosType
        self.assertIs(type(result), KosType)


class TestStubOpcodes(CevalTest):
    """
    Tests for opcodes that have no implementation yet.
    Each test runs code that hits a stub opcode and expects RuntimeError.
    """

    async def _run_stub_opcode(self, opname, arg=0):
        """
        Build and run bytecode that hits the given stub opcode
        """

        code_obj, ns = build_expr([
            [opname, arg],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        return await expr()


    async def test_push_collect_raises_with_invalid_constant(self):
        """
        PUSH_COLLECT with invalid constant (int, not NamespaceSpec)
        raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("PUSH_COLLECT", 0)


    async def test_theend_raises_unimpl(self):
        """
        THEEND has no runtime implementation; raises RuntimeError with fixed message
        """

        with self.assertRaises(RuntimeError) as ctx:
            await self._run_stub_opcode("THEEND", 0)
        self.assertIn("THEEND", str(ctx.exception))
        self.assertIn("no runtime implementation", str(ctx.exception))


    async def test_build_function_stack_underrun_raises(self):
        """
        BUILD_FUNCTION with no fspec on stack raises (stack underrun)
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("BUILD_FUNCTION", 0)


    async def test_call_names_raises_not_implemented(self):
        """
        CALL_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("CALL_NAMES", 0)


    async def test_call_star_raises_not_implemented(self):
        """
        CALL_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("CALL_STAR", 0)


    async def test_call_attr_raises_not_implemented(self):
        """
        CALL_ATTR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("CALL_ATTR", 0)


    async def test_call_attr_names_raises_not_implemented(self):
        """
        CALL_ATTR_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("CALL_ATTR_NAMES", 0)


    async def test_call_attr_star_raises_not_implemented(self):
        """
        CALL_ATTR_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("CALL_ATTR_STAR", 0)


    async def test_tail_call_names_raises_not_implemented(self):
        """
        TAIL_CALL_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("TAIL_CALL_NAMES", 0)


    async def test_tail_call_star_raises_not_implemented(self):
        """
        TAIL_CALL_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("TAIL_CALL_STAR", 0)


    async def test_push_with_raises_not_implemented(self):
        """
        PUSH_WITH raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("PUSH_WITH", "target")


    async def test_push_ns_raises_not_implemented(self):
        """
        PUSH_NS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("PUSH_NS", 0)


    async def test_pop_with_raises_not_implemented(self):
        """
        POP_WITH raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("POP_WITH", 0)


    async def test_pop_ns_raises_not_implemented(self):
        """
        POP_NS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            await self._run_stub_opcode("POP_NS", 0)


# The end.
