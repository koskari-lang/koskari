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
Tests for globals and locals specials
"""


import pytest

from koskari.exceptions import SpecialError
from koskari.lib import ModuleNamespace, OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def _opcodes(code):
    return [code.oparg(i)[0] for i in range(code.length())]


class TestCompileGlobals(SpecialsTestCase):

    async def test_globals_emits_opcode(self):
        code = self.compile_to_code("(globals)")
        self.assertIn(OpCode.GLOBALS, _opcodes(code))


    async def test_globals_rejects_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(globals x)")


    async def test_globals_returns_namespace_with_binding(self):
        expr, mod = self.compile_expr("(globals)", marker=42)
        result = await expr()
        self.assertIsInstance(result, ModuleNamespace)
        # the harness places marker in the builtins, below the module
        self.assertEqual(42, result.resolve("marker"))


    async def test_globals_inside_let_returns_module_namespace(self):
        expr, mod = self.compile_expr(
            "(let [[x 10]] (globals))",
            marker=42)
        result = await expr()
        self.assertIsInstance(result, ModuleNamespace)
        # the harness places marker in the builtins, below the module
        self.assertEqual(42, result.resolve("marker"))


class TestCompileVars(SpecialsTestCase):

    async def test_vars_no_args_emits_locals(self):
        code = self.compile_to_code("(vars)")
        self.assertIn(OpCode.LOCALS, _opcodes(code))


    async def test_vars_object_emits_object_vars(self):
        code = self.compile_to_code("(vars x)", x=1)
        self.assertIn(OpCode.OBJECT_VARS, _opcodes(code))


    async def test_vars_rejects_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(vars x y)")


    async def test_vars_object_dict(self):
        class Thing:
            pass
        thing = Thing()
        thing.k = 1
        expr, mod = self.compile_expr("(vars thing)", thing=thing)
        self.assertEqual({"k": 1}, await expr())


    async def test_vars_returns_namespace_with_binding(self):
        expr, mod = self.compile_expr("(vars)", marker=42)
        result = await expr()
        self.assertIsInstance(result, ModuleNamespace)
        # the harness places marker in the builtins, below the module
        self.assertEqual(42, result.resolve("marker"))


# The end.
