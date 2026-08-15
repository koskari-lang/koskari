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

define-syntax and transformer block syntax (syntax-rules, macro-rules,
syntax-match, macro-match).

These tests only assert that layout expands to the expected cons-tree
(``parse_layout`` / ``LayoutReader.read``). They do not compile or run code.
"""


from unittest import TestCase

import pytest

from koskari.compile.helpers import pycons
from koskari.exceptions import LayoutError
from koskari.lib import symbol

from .. import parse_layout


pytestmark = pytest.mark.phase_infra


class TestLayoutDefineSyntax(TestCase):

    def _assert_layout_parse(self, src, expected):
        result = parse_layout(src)
        print('result   :', result)
        print('expected :', expected)
        self.assertEqual(result, expected)


    def test_define_syntax_layout_parses(self):
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
            (symbol('begin'),
             (symbol('print'), 'x and no args', symbol('x')),
             symbol('x'),))
        rule2 = (
            (symbol('do-something-with-x'), symbol('y')),
            (symbol('begin'),
             (symbol('print'), 'x and one arg', symbol('x'), symbol('y')),
             (symbol('+'), symbol('x'), symbol('y'))))
        expected = pycons(
            (symbol('define-syntax'),
             symbol('do-something-with-x'),
             (symbol('syntax-rules'),
              (symbol('x'),),
              rule1,
              rule2)))
        self._assert_layout_parse(src, expected)


    def test_syntax_rules_match_form(self):
        """
        match (FORM): unwraps to same output as FORM: for s-expr patterns
        """
        src = """
        define-syntax m:
          syntax-rules [item item]:
            case (ref (item obj name)):
              (ref-item obj name)
        """
        rule1 = (
            (symbol('ref'), (symbol('item'), symbol('obj'), symbol('name')),),
            (symbol('begin'),
             (symbol('ref-item'), symbol('obj'), symbol('name')),))
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('syntax-rules'),
              (symbol('item'), symbol('item')),
              rule1)))
        self._assert_layout_parse(src, expected)


    def test_syntax_rules_empty_literals(self):
        src = """
        define-syntax m:
          syntax-rules ():
            m x:
              x
        """
        rule1 = (
            (symbol('m'), symbol('x')),
            (symbol('begin'), symbol('x'),))
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('syntax-rules'),
              (),
              rule1,)))
        self._assert_layout_parse(src, expected)


    def test_define_syntax_layout_double_syntax_rules(self):
        src = """
        define-syntax double:
          syntax-rules ():
            double x:
              (+ x x)
        """
        expected = pycons(
            (symbol('define-syntax'),
             symbol('double'),
             (symbol('syntax-rules'),
              (),
              ((symbol('double'), symbol('x')),
               (symbol('begin'),
                (symbol('+'), symbol('x'), symbol('x'),)))))
        )
        self._assert_layout_parse(src, expected)


    def test_define_syntax_layout_double_literal_in_template(self):
        src = """
        define-syntax double:
          syntax-rules ():
            double x:
              (* x 2)
        """
        expected = pycons(
            (symbol('define-syntax'),
             symbol('double'),
             (symbol('syntax-rules'),
              (),
              ((symbol('double'), symbol('x')),
               (symbol('begin'),
                (symbol('*'), symbol('x'), 2),)))))
        self._assert_layout_parse(src, expected)


    def test_define_syntax_macro_rules_layout(self):
        src = """
        define-syntax m:
          macro-rules ():
            m x:
              x
        """
        rule1 = (
            (symbol('m'), symbol('x')),
            (symbol('begin'), symbol('x'),))
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('macro-rules'),
              (),
              rule1,)))
        self._assert_layout_parse(src, expected)


    def test_define_syntax_special_rules_layout(self):
        src = """
        define-syntax m:
          special-rules ():
            m x:
              x
        """
        rule1 = (
            (symbol('m'), symbol('x')),
            (symbol('begin'), symbol('x'),))
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('special-rules'),
              (),
              rule1,)))
        self._assert_layout_parse(src, expected)


    def test_define_syntax_syntax_match_layout(self):
        src = """
        define-syntax m:
          syntax-match:
            m x:
              x
        """
        rule1 = (
            (symbol('m'), symbol('x')),
            (symbol('begin'), symbol('x'),))
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('syntax-match'),
              rule1,)))
        self._assert_layout_parse(src, expected)


    def test_define_syntax_macro_match_layout(self):
        src = """
        define-syntax m:
          macro-match:
            m x:
              x
        """
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('macro-match'),
              ((symbol('m'), symbol('x')),
               (symbol('begin'), symbol('x'),)))))
        self._assert_layout_parse(src, expected)


    def test_define_syntax_special_match_layout(self):
        src = """
        define-syntax m:
          special-match:
            m x:
              x
        """
        expected = pycons(
            (symbol('define-syntax'),
             symbol('m'),
             (symbol('special-match'),
              ((symbol('m'), symbol('x')),
               (symbol('begin'), symbol('x'),)))))
        self._assert_layout_parse(src, expected)


# The end.
