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
Tests for macros and bindings from koskari.builtins (basics.ki)

Covers compile/set!, compile/ref, car/cdr cxxr macros, and related
macros that require import koskari.builtins so basics.ki has been loaded.
"""


from types import SimpleNamespace

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace, cons, nil
from koskari.reader import default_reader, source_str

import pytest
import unittest


pytestmark = pytest.mark.phase_basics


class TestBasicsModuleLoads(unittest.IsolatedAsyncioTestCase):

    async def test_basics_module_loads(self):
        """
        koskari.builtins includes compile/set! from basics.ki after import
        """

        import koskari.builtins as builtins
        bltns = vars(builtins)

        self.assertIn('compile/set!', bltns, 'compile/set! from basics.ki should be in builtins')


class TestMakeBuiltinsDemand(unittest.IsolatedAsyncioTestCase):
    """
    Tests that import koskari.builtins exposes bootstrap and basics.ki bindings
    """

    def test_make_builtins_demand_returns_builtins_dict(self):
        import koskari.builtins as builtins
        bltns = vars(builtins)

        self.assertIn('cons', bltns)
        self.assertIn('compile/set!', bltns)


class TestRefMacro(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the compile/ref macro in basics.ki

    Requires import koskari.builtins so the macro is present for expansion.
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
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

        expr, mod = self.compile_expr(src_str, deref=deref)
        result = await expr()
        self.assertEqual(10, result)


    async def test_ref_attr_expands_to_ref_attr(self):
        obj = SimpleNamespace(count=5)
        src_str = """
        (compile/call deref (ref (attr obj count)))
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, obj=obj, deref=deref)
        result = await expr()
        self.assertEqual(5, result)


    async def test_ampersand_dotted_expands_to_ref_attr(self):
        obj = SimpleNamespace(count=7)
        src_str = """
        (compile/call deref &obj.count)
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, obj=obj, deref=deref)
        result = await expr()
        self.assertEqual(7, result)


    async def test_ref_item_expands_to_ref_item(self):
        lst = [10, 20, 30]
        src_str = """
        (compile/call deref (ref (item lst 1)))
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, lst=lst, deref=deref)
        result = await expr()
        self.assertEqual(20, result)


class TestLetStarMacro(unittest.IsolatedAsyncioTestCase):
    """
    Tests for compile/let* from basics.ki (nested let expansion)

    Requires import koskari.builtins so the macro is present for expansion.
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


    async def test_let_star_single_binding(self):
        src_str = """
        (let* [[a 1]]
          (compile/call add a 0))
        """

        def add(x, y):
            return x + y

        expr, mod = self.compile_expr(src_str, add=add)
        result = await expr()
        self.assertEqual(1, result)

    async def test_let_star_sequential_bindings(self):
        src_str = """
        (let* [[a 1] [b (compile/+ a 1)]]
          (compile/call add b 0))
        """

        def add(x, y):
            return x + y

        expr, mod = self.compile_expr(src_str, add=add)
        result = await expr()
        self.assertEqual(2, result)


class TestCarCdrMacros(unittest.IsolatedAsyncioTestCase):
    """
    Tests for cxxr macros and first..fifth aliases from basics.ki

    Requires import koskari.builtins so the macros are present for expansion.
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


    async def test_cxxr_accessors(self):
        src_str = """
        (let [[lst (compile/cons 1 2 3 4 5 nil)]]
          (compile/cons
            (compile/car lst)
            (compile/cadr lst)
            (compile/caddr lst)
            (compile/cadddr lst)
            (compile/caddddr lst)
            (compile/cdr lst)
            (compile/cddr lst)
            (compile/cdddr lst)
            (compile/cddddr lst)
            nil))
        """

        expr, mod = self.compile_expr(src_str)
        result = await expr()
        car_val, tail = result
        cadr_val, tail = tail
        caddr_val, tail = tail
        cadddr_val, tail = tail
        caddddr_val, tail = tail
        cdr_val, tail = tail
        cddr_val, tail = tail
        cdddr_val, tail = tail
        cddddr_val, tail = tail
        self.assertEqual(1, car_val)
        self.assertEqual(2, cadr_val)
        self.assertEqual(3, caddr_val)
        self.assertEqual(4, cadddr_val)
        self.assertEqual(5, caddddr_val)
        self.assertEqual([2, 3, 4, 5], list(cdr_val.unpack()))
        self.assertEqual([3, 4, 5], list(cddr_val.unpack()))
        self.assertEqual([4, 5], list(cdddr_val.unpack()))
        self.assertEqual([5], list(cddddr_val.unpack()))


    async def test_first_through_fifth_aliases(self):
        src_str = """
        (let [[lst (compile/cons 10 20 30 40 50 nil)]]
          (compile/cons
            (compile/first lst)
            (compile/second lst)
            (compile/third lst)
            (compile/fourth lst)
            (compile/fifth lst)
            nil))
        """

        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(cons(10, 20, 30, 40, 50, nil), result)


class TestSetBangPairMacros(unittest.IsolatedAsyncioTestCase):
    """
    Tests for compile/set! cxxr cases from basics.ki

    Requires import koskari.builtins so the macro is present for expansion.
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


    async def test_set_bang_car_and_cdr(self):
        src_str = """
        (let [[lst (compile/cons 1 2 nil)]]
          (compile/set! (car lst) 9)
          (compile/set! (cdr lst) (compile/cons 8 nil))
          lst)
        """

        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(cons(9, 8, nil), result)


    async def test_set_bang_cxxr_positions(self):
        src_str = """
        (let [[lst (compile/cons 1 2 3 4 5 nil)]]
          (compile/set! (cadr lst) 20)
          (compile/set! (caddr lst) 30)
          (compile/set! (cadddr lst) 40)
          (compile/set! (caddddr lst) 50)
          lst)
        """

        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(cons(1, 20, 30, 40, 50, nil), result)


    async def test_set_bang_cxxr_tail_positions(self):
        src_cddr = """
        (let [[lst (compile/cons 1 2 3 nil)]]
          (compile/set! (cddr lst) (compile/cons 9 8 nil))
          lst)
        """
        expr, mod = self.compile_expr(src_cddr)
        self.assertEqual(cons(1, 2, 9, 8, nil), await expr())

        src_cdddr = """
        (let [[lst (compile/cons 1 2 3 4 nil)]]
          (compile/set! (cdddr lst) (compile/cons 9 8 nil))
          lst)
        """
        expr, mod = self.compile_expr(src_cdddr)
        self.assertEqual(cons(1, 2, 3, 9, 8, nil), await expr())

        src_cddddr = """
        (let [[lst (compile/cons 1 2 3 4 5 nil)]]
          (compile/set! (cddddr lst) (compile/cons 9 nil))
          lst)
        """
        expr, mod = self.compile_expr(src_cddddr)
        self.assertEqual(cons(1, 2, 3, 4, 9, nil), await expr())


class TestConditionalMacros(unittest.IsolatedAsyncioTestCase):
    """
    Tests for compile/if, compile/when, and compile/unless from basics.ki

    Requires import koskari.builtins so the macros are present for expansion.
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


    async def test_if_then_else(self):
        src_str_t = "(compile/if True 1 2)"
        expr, mod = self.compile_expr(src_str_t)
        self.assertEqual(await expr(), 1)

        src_str_f = "(compile/if False 1 2)"
        expr, mod = self.compile_expr(src_str_f)
        self.assertEqual(await expr(), 2)


    async def test_when(self):
        src_str_t = "(compile/when True 42)"
        expr, mod = self.compile_expr(src_str_t)
        self.assertEqual(await expr(), 42)

        src_str_f = "(compile/when False 42)"
        expr, mod = self.compile_expr(src_str_f)
        self.assertEqual(await expr(), None)


    async def test_unless(self):
        src_str_f = "(compile/unless False 42)"
        expr, mod = self.compile_expr(src_str_f)
        self.assertEqual(await expr(), 42)

        src_str_t = "(compile/unless True 42)"
        expr, mod = self.compile_expr(src_str_t)
        self.assertEqual(await expr(), None)


# The end.
