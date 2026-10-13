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
tests.importer.test_list_each_filter

list-each whole-body filter contract: a body that is a single `when` or
`unless` form collects inside the conditional (no None gaps). The
behavior applies only when the conditional encompasses the whole body; a
multi-form body keeps plain value semantics, filters do not compose
through nesting, and the keyword clause forms are unchanged.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestListEachFilter(unittest.IsolatedAsyncioTestCase):
    """
    list-each with a whole-body when/unless is a filter
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def _eval(self, src_str):
        expr, _mod = self.compile_expr(src_str)
        return await expr()


    async def test_whole_body_when_filters(self):
        """
        a single when body collects only matching elements, no None gaps
        """

        result = await self._eval(
            '(list-each [x (range 0 10)] (when (& x 1) (* x 100)))')
        self.assertEqual([100, 300, 500, 700, 900], result)


    async def test_whole_body_unless_filters(self):
        """
        the unless twin skips matching elements
        """

        result = await self._eval(
            '(list-each [x (range 0 5)] (unless (& x 1) x))')
        self.assertEqual([0, 2, 4], result)


    async def test_when_multi_form_conditional_body(self):
        """
        multiple forms inside the when are its body; the last is collected
        """

        result = await self._eval(
            '(list-each [x (range 0 4)] (when (& x 1) (var y (* x 10)) y))')
        self.assertEqual([10, 30], result)


    async def test_plain_body_unchanged(self):
        """
        an unconditional body still collects every element
        """

        result = await self._eval(
            '(list-each [x (range 0 3)] (* x 2))')
        self.assertEqual([0, 2, 4], result)


    async def test_multi_form_body_gets_no_injection(self):
        """
        the filter applies only when the conditional encompasses the WHOLE
        body; a trailing when in a multi-form body is a plain expression,
        so misses collect None
        """

        result = await self._eval(
            '(list-each [x (range 0 4)] (var y x) (when (& x 1) x))')
        self.assertEqual([None, 1, None, 3], result)


    async def test_nested_when_does_not_compose(self):
        """
        filters do not compose: a nested when is an ordinary expression
        whose None lands in the list -- (when (and a b) ...) is the
        composite check
        """

        result = await self._eval(
            '(list-each [x (range 0 4)] (when True (when (& x 1) x)))')
        self.assertEqual([None, 1, None, 3], result)


    async def test_keyword_clause_forms_unchanged(self):
        """
        the when:/unless: clause spellings keep their behavior
        """

        result = await self._eval(
            '(list-each [x (range 0 10)] when: (& x 1) x)')
        self.assertEqual([1, 3, 5, 7, 9], result)

        result = await self._eval(
            '(list-each [x (range 0 5)] unless: (& x 1) x)')
        self.assertEqual([0, 2, 4], result)




class TestListEachAccumulator(TestListEachFilter):
    """
    The list-each accumulator is the form's own: in a class body it is
    no attribute of the class, and nested list-each forms each have
    their own
    """

    async def test_class_body_keeps_its_col_attribute(self):
        src = """
        (begin
          (define-class Table []
            (var col "name")
            (var widths (list-each [w #[3 5]] (* w 2))))
          #(Table.widths (getattr Table "col" "<deleted>")))
        """
        self.assertEqual(([6, 10], "name"), await self._eval(src))


    async def test_nested_in_class_body(self):
        src = """
        (begin
          (define-class Grid []
            (var cells (list-each [x #[1 2]]
                            (list-each [y #[10 20]] (+ x y)))))
          Grid.cells)
        """
        self.assertEqual([[11, 21], [12, 22]], await self._eval(src))


    async def test_nested_at_module_level(self):
        src = """
        (list-each [x #[1 2]] (list-each [y #[10 20]] (+ x y)))
        """
        self.assertEqual([[11, 21], [12, 22]], await self._eval(src))


    async def test_filter_arms_in_class_body(self):
        src = """
        (begin
          (define-class F []
            (var odds (list-each [x (range 0 6)] (when (& x 1) x)))
            (var evens (list-each [x (range 0 6)] (unless (& x 1) x))))
          #(F.odds F.evens))
        """
        self.assertEqual(([1, 3, 5], [0, 2, 4]), await self._eval(src))


    async def test_sequence_is_a_class_body_name(self):
        src = """
        (begin
          (define-class C []
            (define names #["a" "b"])
            (define upper (list-each [n names] (n.upper))))
          C.upper)
        """
        self.assertEqual(["A", "B"], await self._eval(src))


# The end.
