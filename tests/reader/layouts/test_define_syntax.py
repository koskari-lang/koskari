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
Tests for koskari.reader.layouts.define_syntax layout handlers.

define-syntax and syntax-rules block syntax.
"""


from unittest import IsolatedAsyncioTestCase

from koskari.dis import dis
from koskari.importer.bootstrap import make_bootstrap
from koskari.reader import LayoutReader, source_str
from koskari.compile import Compiler
from koskari.types import Module, ModuleNamespace
from koskari.types import is_pair, nil, symbol
from koskari.compile.helpers import pycons

from .. import parse_layout


class TestLayoutDefineSyntax(IsolatedAsyncioTestCase):

    async def test_define_syntax_layout_parses(self):
        src = """
        define-syntax do-something-with-x:
          syntax-rules [x]:
            do-something-with-x:
              (print "x and no args" x)
              x
            do-something-with-x y:
              (print "x and one arg" x y)
              (+ x y)
        """
        rule1 = (
            (symbol('do-something-with-x'),),
            (symbol('compile/begin'),
             (symbol('print'), 'x and no args', symbol('x')),
             symbol('x'),),)
        rule2 = (
            (symbol('do-something-with-x'), symbol('y')),
            (symbol('compile/begin'),
             (symbol('print'), 'x and one arg', symbol('x'), symbol('y')),
             (symbol('+'), symbol('x'), symbol('y'))))
        expected = pycons(
            (symbol('compile/define-syntax'),
             symbol('do-something-with-x'),
             (symbol('syntax-rules'),
              (symbol('x'),),
              rule1,
              rule2)))
        result = parse_layout(src)
        print('result   :', result)
        print('expected :', expected)
        self.assertEqual(result, expected)


    async def test_syntax_rules_empty_literals(self):
        src = """
        define-syntax m:
          syntax-rules ():
            m x:
              x
        """
        rule1 = (
            (symbol('m'), symbol('x')),
            (symbol('compile/begin'), symbol('x'),))
        expected = pycons(
            (symbol('compile/define-syntax'),
             symbol('m'),
             (symbol('syntax-rules'),
              (),
              rule1,)))
        result = parse_layout(src)
        print('result   :', result)
        print('expected :', expected)
        self.assertEqual(result, expected)


    async def test_define_syntax_layout_compiles_and_runs(self):
        src = """
        define-syntax double:
          syntax-rules ():
            double x:
              (+ x x)
        """
        expected = pycons(
            (symbol('compile/define-syntax'),
             symbol('double'),
             (symbol('syntax-rules'),
              (),
              ((symbol('double'), symbol('x')),
               (symbol('compile/begin'),
                (symbol('+'), symbol('x'), symbol('x'),)))))
        )

        result = parse_layout(src)
        print('result   :', result)
        print('expected :', expected)
        self.assertEqual(result, expected)

        builtins = make_bootstrap()
        reader = LayoutReader(builtins)
        stream = source_str(src, __file__)
        source = reader.read(stream)

        sr = source[1][1][0]
        rules = list(sr[1][1].unpack())
        self.assertEqual(len(rules), 1, 'expect one rule')
        rule = rules[0]
        pattern_expr, template_tail = rule
        self.assertTrue(is_pair(template_tail),
                        'rule must be (pattern (template)); template_tail=%r' % (template_tail,))
        self.assertTrue(
            template_tail[1] is nil if is_pair(template_tail) else False,
            'template_tail must be single-element list; template_tail=%r' % (template_tail,))

        module = Module(ModuleNamespace('test', builtins))
        compiler = Compiler('<unittest>', module, filename=__file__)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(vars(module))
        await expr()

        use_src = """
        (double 21)
        """
        use_reader = LayoutReader(vars(module))
        use_stream = source_str(use_src, __file__)
        use_source = use_reader.read(use_stream)
        use_fragment = compiler.full_compile(use_source)
        use_expr = use_fragment.as_expression(vars(module))

        dis(use_expr.code)

        result = await use_expr()
        self.assertEqual(42, result)


    async def test_define_syntax_template_with_literal_number(self):
        src = """
        define-syntax double:
          syntax-rules ():
            double x:
              (* x 2)
        """

        builtins = make_bootstrap()
        reader = LayoutReader(builtins)
        stream = source_str(src, __file__)
        source = reader.read(stream)
        module = Module(ModuleNamespace('test', builtins))
        compiler = Compiler('<unittest>', module, filename=__file__)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(vars(module))
        await expr()

        use_src = "(double 21)"
        use_reader = LayoutReader(vars(module))
        use_stream = source_str(use_src, __file__)
        use_source = use_reader.read(use_stream)
        use_fragment = compiler.full_compile(use_source)
        use_expr = use_fragment.as_expression(vars(module))
        result = await use_expr()
        self.assertEqual(42, result)


    async def test_define_syntax_requires_single_syntax_rules(self):
        src = """
        define-syntax m:
          syntax-rules ():
            m x:
              x
          (extra form)
        """

        with self.assertRaises(Exception):
            parse_layout(src)


# The end.
