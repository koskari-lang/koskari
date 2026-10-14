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
Tests for the var pattern arms, def, the global prefix forms, syntax,
and their decorator arms from koskari.builtins
"""


import unittest
from textwrap import dedent

import pytest

from koskari.exceptions import SyntaxMatchError
from koskari.lib import demand_call
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_basics


class BindingFormsCase(unittest.IsolatedAsyncioTestCase):

    async def load(self, src):
        module = new_module("bindingformstest")
        ns = vars(module)
        ns['__file__'] = "<bindingformstest>"
        await load_module_source_async(module, dedent(src))
        return ns


class TestVarPattern(BindingFormsCase):

    async def test_flat_pattern(self):
        ns = await self.load("""
        var [a b] #[1 2]
        """)
        self.assertEqual((1, 2), (ns['a'], ns['b']))


    async def test_nested_pattern_and_rest(self):
        ns = await self.load("""
        var [a [b c] *: d] #[1 #[2 3] 4 5]
        """)
        self.assertEqual(
            (1, 2, 3, (4, 5)),
            (ns['a'], ns['b'], ns['c'], ns['d']))


    async def test_pattern_without_value_declares(self):
        ns = await self.load("""
        def f []:
          var [a b]
          set! a 1
          set! b 2
          #(a b)
        """)
        self.assertEqual((1, 2), demand_call(ns['f']))


    async def test_pattern_in_function_body(self):
        ns = await self.load("""
        def f [seq]:
          var [x *: rest] seq
          #(x rest)
        """)
        self.assertEqual((1, (2, 3)), demand_call(ns['f'], [1, 2, 3]))
        self.assertNotIn('x', ns)


    async def test_pattern_in_class_body(self):
        ns = await self.load("""
        var K:
          build-class K []:
            var [p q] #[3 4]
        """)
        self.assertEqual((3, 4), (ns['K'].p, ns['K'].q))


class TestDef(BindingFormsCase):

    async def test_def(self):
        ns = await self.load("""
        def add [a b]:
          + a b
        """)
        self.assertEqual(3, demand_call(ns['add'], 1, 2))
        self.assertEqual('add', ns['add'].__name__)


    async def test_def_typed(self):
        ns = await self.load("""
        def double :: int [x]:
          * x 2
        """)
        self.assertEqual(8, demand_call(ns['double'], 4))
        self.assertIs(int, ns['double'].__annotations__['return'])


    async def test_def_in_function_body_is_local(self):
        ns = await self.load("""
        def outer []:
          def inner [y]:
            + y 1
          inner 1
        """)
        self.assertEqual(2, demand_call(ns['outer']))
        self.assertNotIn('inner', ns)


    async def test_def_recursion(self):
        ns = await self.load("""
        def fact [n]:
          if (<= n 1) 1 (* n (fact (- n 1)))
        """)
        self.assertEqual(120, demand_call(ns['fact'], 5))


    async def test_def_value_is_wrong_usage(self):
        with self.assertRaises(SyntaxMatchError):
            await self.load("""
            def x 1
            """)


class TestGlobalPrefix(BindingFormsCase):

    async def test_global_var(self):
        ns = await self.load("""
        var count 5
        def zero []:
          global var count 0
        (zero)
        """)
        self.assertEqual(0, ns['count'])
        self.assertNotIn('var', ns)


    async def test_global_var_declare_then_store(self):
        ns = await self.load("""
        var hits 0
        def bump []:
          global var hits
          set! hits (+ hits 1)
        (bump)
        (bump)
        """)
        self.assertEqual(2, ns['hits'])


    async def test_global_var_typed(self):
        ns = await self.load("""
        def setup []:
          global var g :: int 9
        (setup)
        """)
        self.assertEqual(9, ns['g'])


    async def test_global_bare_name_is_wrong_usage(self):
        with self.assertRaises(SyntaxMatchError):
            await self.load("""
            def setup []:
              global x 1
            """)


    async def test_global_def(self):
        ns = await self.load("""
        def setup []:
          global def helper [y]:
            + y 100
        (setup)
        """)
        self.assertEqual(101, demand_call(ns['helper'], 1))


    async def test_global_class(self):
        ns = await self.load("""
        def setup []:
          global class Box []:
            var size 3
        (setup)
        """)
        self.assertEqual(3, ns['Box'].size)
        self.assertEqual('Box', ns['Box'].__name__)


    async def test_global_pattern(self):
        ns = await self.load("""
        def setup []:
          global var [p q] #[8 9]
        (setup)
        """)
        self.assertEqual((8, 9), (ns['p'], ns['q']))


    async def test_global_pattern_without_value(self):
        ns = await self.load("""
        def setup []:
          global var [p q]
          set! p 1
          set! q 2
        (setup)
        """)
        self.assertEqual((1, 2), (ns['p'], ns['q']))


    async def test_global_syntax(self):
        ns = await self.load("""
        def setup []:
          global syntax twice:
            syntax-rules []:
              case (twice x):
                (+ x x)
        (setup)
        var result (twice 4)
        """)
        self.assertEqual(8, ns['result'])


class TestSyntax(BindingFormsCase):

    async def test_top_level_syntax(self):
        ns = await self.load("""
        syntax twice:
          syntax-rules []:
            case (twice x):
              (+ x x)
        var result (twice 4)
        """)
        self.assertEqual(8, ns['result'])
        self.assertIn('compile/twice', ns)


    async def test_top_level_syntax_docstring(self):
        ns = await self.load("""
        syntax twice:
          "Doubles its argument"
          syntax-rules []:
            case (twice x):
              (+ x x)
        var result (twice 5)
        """)
        self.assertEqual(10, ns['result'])


    async def test_body_syntax(self):
        ns = await self.load("""
        def f []:
          syntax thrice:
            syntax-rules []:
              case (thrice x):
                (+ x x x)
          thrice 5
        """)
        self.assertEqual(15, demand_call(ns['f']))
        self.assertNotIn('compile/thrice', ns)


    async def test_body_syntax_ends_with_body(self):
        with self.assertRaises(NameError):
            await self.load("""
            def f []:
              syntax thrice:
                syntax-rules []:
                  case (thrice x):
                    (+ x x x)
              thrice 5
            thrice 1
            """)


    async def test_body_syntax_shadows_for_rest_of_body(self):
        ns = await self.load("""
        syntax twice:
          syntax-rules []:
            case (twice x):
              (+ x x)
        def f []:
          var before (twice 3)
          syntax twice:
            syntax-rules []:
              case (twice x):
                (* x 100)
          #(before (twice 3))
        var after (twice 3)
        """)
        self.assertEqual((6, 300), demand_call(ns['f']))
        self.assertEqual(6, ns['after'])


class TestClass(BindingFormsCase):

    async def test_class_binds_its_name(self):
        ns = await self.load("""
        class Box []:
          var size 3
        """)
        self.assertEqual('Box', ns['Box'].__name__)
        self.assertEqual(3, ns['Box'].size)


    async def test_class_supers(self):
        ns = await self.load("""
        class Base []:
          var k 1
        class Sub [Base]:
          pass
        """)
        self.assertEqual((ns['Base'],), ns['Sub'].__bases__)
        self.assertEqual(1, ns['Sub'].k)


    async def test_class_makes_no_predicate(self):
        ns = await self.load("""
        class Box []:
          pass
        """)
        self.assertNotIn('Box?', ns)


    async def test_class_in_function_body_is_local(self):
        ns = await self.load("""
        def make []:
          class Inner []:
            var k 2
          Inner
        """)
        self.assertEqual(2, demand_call(ns['make']).k)
        self.assertNotIn('Inner', ns)


    async def test_decorated_class(self):
        ns = await self.load("""
        def tag [cls]:
          set! cls.tagged True
          cls

        @tag
        class Thing []:
          var k 1
        """)
        self.assertTrue(ns['Thing'].tagged)


    async def test_dataclass_over_class(self):
        ns = await self.load("""
        import-from dataclasses dataclass

        @dataclass
        class Point []:
          var x :: int 0
          var y :: int 0
        """)
        point = ns['Point'](1, 2)
        self.assertEqual((1, 2), (point.x, point.y))


class TestDecorators(BindingFormsCase):

    PRELUDE = """
    def wrap [f]:
      lambda [x]:
        + 1000 (f x)
    """


    async def test_decorated_def(self):
        ns = await self.load(dedent(self.PRELUDE) + dedent("""
        @wrap
        def plus1 [x]:
          + x 1
        """))
        self.assertEqual(1002, demand_call(ns['plus1'], 1))


    async def test_decorated_scheme_define(self):
        ns = await self.load(dedent(self.PRELUDE) + dedent("""
        @wrap
        define [plus2 x]:
          + x 2
        """))
        self.assertEqual(1003, demand_call(ns['plus2'], 1))


    async def test_decorated_typed_scheme_define(self):
        ns = await self.load(dedent(self.PRELUDE) + dedent("""
        @wrap
        define [(plus3 :: int) x]:
          + x 3
        """))
        self.assertEqual(1004, demand_call(ns['plus3'], 1))


    async def test_decorated_var(self):
        ns = await self.load(dedent(self.PRELUDE) + dedent("""
        @wrap
        var ident:
          lambda [x] x
        """))
        self.assertEqual(1005, demand_call(ns['ident'], 5))


    async def test_decorated_global_var(self):
        ns = await self.load(dedent(self.PRELUDE) + dedent("""
        def setup []:
          @wrap
          global var gident:
            lambda [x] x
        (setup)
        """))
        self.assertEqual(1007, demand_call(ns['gident'], 7))


    async def test_decorated_global_def(self):
        ns = await self.load(dedent(self.PRELUDE) + dedent("""
        def setup []:
          @wrap
          global def gplus [x]:
            + x 2
        (setup)
        """))
        self.assertEqual(1003, demand_call(ns['gplus'], 1))


    async def test_decorated_global_class(self):
        ns = await self.load("""
        def tag [cls]:
          set! cls.tagged True
          cls

        def setup []:
          @tag
          global class Thing []:
            var k 1
        (setup)
        """)
        self.assertTrue(ns['Thing'].tagged)


# The end.
