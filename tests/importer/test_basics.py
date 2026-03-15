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
Tests for koskari.importer.basics (koskari-written builtins in basics.ki)

Tests for compile/set!, compile/ref (and other macros) that require the
full builtins (make_builtins) to be loaded.
"""


from types import SimpleNamespace

from koskari.compile import Compiler
from koskari.importer.bootstrap import make_builtins, make_builtins_demand
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str

import pytest
import unittest


pytestmark = pytest.mark.phase_basics


class TestBasicsModuleLoads(unittest.IsolatedAsyncioTestCase):

    async def test_basics_module_loads(self):
        """
        make_builtins loads basics.ki; compile/set! from basics.ki is present
        """
        builtins = await make_builtins()
        self.assertIn('compile/set!', builtins, 'compile/set! from basics.ki should be in builtins')


class TestMakeBuiltinsDemand(unittest.IsolatedAsyncioTestCase):
    """Tests for make_builtins_demand (demand path, shares cache with make_builtins)."""

    def test_make_builtins_demand_returns_builtins_dict(self):
        builtins = make_builtins_demand()
        self.assertIn('cons', builtins)
        self.assertIn('compile/set!', builtins)

    async def test_shared_cache_with_make_builtins(self):
        demand_builtins = make_builtins_demand()
        async_builtins = await make_builtins()
        self.assertIs(demand_builtins, async_builtins,
                      'make_builtins and make_builtins_demand share _BUILTINS_CACHE')


class TestRefMacro(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the compile/ref macro in basics.ki. Requires make_builtins
    so the macro is present for expansion.
    """

    async def compile_expr(self, src_str, lineno=1, **defns):
        bltns = dict(await make_builtins())
        bltns.update(defns)
        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


    async def test_ref_symbol_expands_to_refq(self):
        src_str = """
        (let [[x 10]]
          (compile/call deref (ref x)))
        """
        def deref(r):
            return r.deref()

        expr, mod = await self.compile_expr(src_str, deref=deref)
        result = await expr()
        self.assertEqual(10, result)


    async def test_ref_attr_expands_to_ref_attr(self):
        obj = SimpleNamespace(count=5)
        src_str = """
        (compile/call deref (ref (attr obj count)))
        """
        def deref(r):
            return r.deref()

        expr, mod = await self.compile_expr(src_str, obj=obj, deref=deref)
        result = await expr()
        self.assertEqual(5, result)


    async def test_ampersand_dotted_expands_to_ref_attr(self):
        obj = SimpleNamespace(count=7)
        src_str = """
        (compile/call deref &obj.count)
        """
        def deref(r):
            return r.deref()

        expr, mod = await self.compile_expr(src_str, obj=obj, deref=deref)
        result = await expr()
        self.assertEqual(7, result)


    async def test_ref_item_expands_to_ref_item(self):
        lst = [10, 20, 30]
        src_str = """
        (compile/call deref (ref (item lst 1)))
        """
        def deref(r):
            return r.deref()

        expr, mod = await self.compile_expr(src_str, lst=lst, deref=deref)
        result = await expr()
        self.assertEqual(20, result)


class TestConditionalMacros(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the compile/if, compile/when, compile/unless macros in basics.ki.
    Requires make_builtins so the macros are present for expansion.
    """

    async def compile_expr(self, src_str, lineno=1, **defns):
        bltns = dict(await make_builtins())
        bltns.update(defns)
        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


    async def test_if_then_else(self):
        src_str_t = "(compile/if True 1 2)"
        expr, mod = await self.compile_expr(src_str_t)
        self.assertEqual(await expr(), 1)

        src_str_f = "(compile/if False 1 2)"
        expr, mod = await self.compile_expr(src_str_f)
        self.assertEqual(await expr(), 2)


    async def test_when(self):
        src_str_t = "(compile/when True 42)"
        expr, mod = await self.compile_expr(src_str_t)
        self.assertEqual(await expr(), 42)

        src_str_f = "(compile/when False 42)"
        expr, mod = await self.compile_expr(src_str_f)
        self.assertEqual(await expr(), None)


    async def test_unless(self):
        src_str_f = "(compile/unless False 42)"
        expr, mod = await self.compile_expr(src_str_f)
        self.assertEqual(await expr(), 42)

        src_str_t = "(compile/unless True 42)"
        expr, mod = await self.compile_expr(src_str_t)
        self.assertEqual(await expr(), None)


# The end.
