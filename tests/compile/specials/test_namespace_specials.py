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
Tests for compile/globals and compile/locals specials
"""


from koskari.exceptions import SpecialError
from koskari.lib import OpCode
from koskari.lib import ModuleNamespace

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


def _opcodes(code):
    return [code.oparg(i)[0] for i in range(code.length())]


class TestCompileGlobals(SpecialsTestCase):

    async def test_globals_emits_opcode(self):
        code = self.compile_to_code("(compile/globals)")
        self.assertIn(OpCode.KOS_OP_GLOBALS, _opcodes(code))


    async def test_globals_rejects_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/globals x)")


    async def test_globals_returns_namespace_with_binding(self):
        expr, mod = self.compile_expr("(compile/globals)", marker=42)
        result = await expr()
        self.assertIsInstance(result, ModuleNamespace)
        self.assertEqual(42, result["marker"])


    async def test_globals_inside_let_returns_module_namespace(self):
        expr, mod = self.compile_expr(
            "(compile/let [[x 10]] (compile/globals))",
            marker=42)
        result = await expr()
        self.assertIsInstance(result, ModuleNamespace)
        self.assertEqual(42, result["marker"])


class TestCompileLocals(SpecialsTestCase):

    async def test_locals_emits_opcode(self):
        code = self.compile_to_code("(compile/locals)")
        self.assertIn(OpCode.KOS_OP_LOCALS, _opcodes(code))


    async def test_locals_rejects_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/locals x)")


    async def test_locals_returns_namespace_with_binding(self):
        expr, mod = self.compile_expr("(compile/locals)", marker=42)
        result = await expr()
        self.assertIsInstance(result, ModuleNamespace)
        self.assertEqual(42, result["marker"])


# The end.
