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
Tests for template expansion in koskari.hygiene: substitution,
ellipsis repetition, and delimiter preservation, without marking

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.exceptions import CompilerError
from koskari.hygiene import (
    equal_by_base, expand_template_with_hygiene, mark_expansion)
from koskari.lib import (
    bracket_cons, bracket_nil, bracket_pair, cons, gensym, keyword, kos_repr,
    nil, pair, symbol)
from koskari.types import build_proper


pytestmark = pytest.mark.phase_infra


class TestExpandTemplate(unittest.TestCase):


    def test_non_pair_atom_passes_through(self):
        """
        Non-pair, non-symbol templates are opaque like Syntax boxes
        """

        sentinel = object()
        out = expand_template_with_hygiene(sentinel, {})
        self.assertIs(out, sentinel)


    def test_ellipsis_incompatible_lengths(self):
        """
        Ellipsis repetition requires uniform list lengths across symbols in
        the repeated segment
        """

        sa = symbol("a")
        sb = symbol("b")
        inner = cons(sa, sb, nil)
        tpl = cons(inner, Ellipsis, nil)
        bindings = {sa: [1], sb: [1, 2]}
        with self.assertRaises(CompilerError) as ctx:
            expand_template_with_hygiene(tpl, bindings)
        self.assertIn("ellipsis", str(ctx.exception).lower())


    def test_template_ellipsis_splices_tail_binding(self):
        """
        Lone pattern-variable then ``...`` splices like quasiquote ``,@``
        when the binding is not a Python list
        """

        plus = symbol("+")
        xs = symbol("xs")
        tpl = build_proper(plus, xs, Ellipsis)
        bindings = {xs: cons(10, 20, nil)}
        out = expand_template_with_hygiene(tpl, bindings)
        self.assertEqual(out, build_proper(plus, 10, 20))


    def test_bracket_template_preserves_brackets(self):
        """a [ ] template expands to a bracket list, not a flattened paren one"""

        # template: (compile/cond [condition then]) -- the `if` macro shape
        cond = symbol("condition")
        then = symbol("then")
        inner = bracket_pair(cond, bracket_pair(then, bracket_nil))
        tpl = build_proper(symbol("cond"), inner)
        out = expand_template_with_hygiene(
            tpl, {cond: symbol("foo"), then: 100})

        self.assertIs(type(out), pair)            # outer form stays paren
        inner_out = out.cdr.car
        self.assertIs(type(inner_out), bracket_pair)   # inner stays bracket
        self.assertEqual(kos_repr(out), "(cond [foo 100])")


    def test_template_ellipsis_list_binding_uses_repeat_count(self):
        """
        List-valued bindings keep indexed ellipsis repetition
        """

        n = symbol("n")
        tpl = build_proper(n, Ellipsis)
        bindings = {n: [11, 22]}
        out = expand_template_with_hygiene(tpl, bindings)
        self.assertEqual(out, build_proper(11, 22))


    def test_template_ellipsis_repeats_compound_fn_a_b(self):
        """
        ``(#list (fn a b) ...)`` repeats the compound ``(fn a b)`` once per
        index when ``a`` and ``b`` are list-valued captures of equal length
        """

        hash_list = symbol("#list")
        fn = symbol("fn")
        a = symbol("a")
        b = symbol("b")
        plus = symbol("+")
        compound = cons(fn, a, b, nil)
        tpl = cons(hash_list, compound, ..., nil)
        bindings = {
            fn: plus,
            a: [1, 3, 100, 5],
            b: [2, 4, 200, -1],
        }
        out = expand_template_with_hygiene(tpl, bindings)
        want = cons(
            hash_list,
            cons(plus, 1, 2, nil),
            cons(plus, 3, 4, nil),
            cons(plus, 100, 200, nil),
            cons(plus, 5, -1, nil),
            nil,
        )
        self.assertEqual(out, want)


    def test_nested_ellipsis_identity(self):
        """
        A rank-2 variable under two template ellipses peels one dimension per
        level: ``(result (a b ...) ...)`` reconstructs the grouped shape
        """

        result = symbol("result")
        a = symbol("a")
        b = symbol("b")
        inner = cons(a, b, ..., nil)
        tpl = cons(result, inner, ..., nil)
        bindings = {
            a: [1, 4],
            b: [[2, 3], [5]],
        }
        out = expand_template_with_hygiene(tpl, bindings)
        want = cons(
            result,
            cons(1, 2, 3, nil),
            cons(4, 5, nil),
            nil,
        )
        self.assertEqual(out, want)


    def test_nested_ellipsis_inner_only(self):
        """
        A rank-2 variable whose outer group carries no rank-1 sibling still
        iterates both dimensions from its own nesting
        """

        result = symbol("result")
        b = symbol("b")
        inner = cons(b, ..., nil)
        tpl = cons(result, inner, ..., nil)
        bindings = {b: [[1, 2], [], [3]]}
        out = expand_template_with_hygiene(tpl, bindings)
        want = cons(
            result,
            cons(1, 2, nil),
            nil,
            cons(3, nil),
            nil,
        )
        self.assertEqual(out, want)


    def test_nested_ellipsis_insufficient_depth_errors(self):
        """
        Referencing a rank-2 variable under a single ellipsis leaves an
        unconsumed list dimension; that is a compile-time error, not a
        silent mis-splice
        """

        result = symbol("result")
        b = symbol("b")
        tpl = cons(result, b, ..., nil)
        bindings = {b: [[1, 2], [3]]}
        with self.assertRaises(CompilerError) as ctx:
            expand_template_with_hygiene(tpl, bindings)
        self.assertIn("ellipsis", str(ctx.exception).lower())


class TestMarkExpansion(unittest.TestCase):


    def test_cyclic_output_is_refused(self):
        """
        A procedural arm's output that contains itself is refused
        rather than walked without end
        """

        cell = cons(1, nil)
        cell.cdr = cell
        with self.assertRaises(CompilerError) as caught:
            mark_expansion(cell, 1)
        self.assertIn("cyclic", str(caught.exception))


    def test_car_cycle_is_refused(self):
        outer = cons(1, nil)
        inner = cons(outer, nil)
        outer.car = inner
        with self.assertRaises(CompilerError):
            mark_expansion(outer, 1)


class TestEqualByBase(unittest.TestCase):
    """
    equal_by_base compares two forms with every gensym read as its
    base, walking the spines without copying
    """

    def test_marked_form_equals_bare_form(self):
        t = gensym(symbol("t"), marks=(1,))
        marked = cons(symbol("let"), cons(t, 1, nil), t, nil)
        bare = cons(symbol("let"), cons(symbol("t"), 1, nil), symbol("t"),
                    nil)
        self.assertTrue(equal_by_base(marked, bare))
        self.assertTrue(equal_by_base(bare, marked))


    def test_different_bases_differ(self):
        t = gensym(symbol("t"), marks=(1,))
        self.assertFalse(equal_by_base(cons(t, nil), cons(symbol("u"), nil)))


    def test_two_gensyms_compare_by_base(self):
        a = gensym(symbol("t"), marks=(1,))
        b = gensym(symbol("t"), marks=(2,))
        self.assertTrue(equal_by_base(a, b))
        self.assertTrue(equal_by_base(cons(a, 1, nil), cons(b, 1, nil)))


    def test_length_and_terminator(self):
        self.assertFalse(equal_by_base(cons(1, 2, nil), cons(1, nil)))
        self.assertFalse(equal_by_base(cons(1, nil), cons(1, 2, nil)))
        self.assertTrue(equal_by_base(cons(1, nil), bracket_cons(1)))
        self.assertTrue(equal_by_base(nil, nil))


    def test_pair_against_atom(self):
        self.assertFalse(equal_by_base(cons(1, nil), 1))
        self.assertFalse(equal_by_base(symbol("t"), cons(symbol("t"), nil)))


    def test_other_values_compare_by_equality(self):
        self.assertTrue(equal_by_base(cons(1, "a", nil), cons(1, "a", nil)))
        self.assertFalse(equal_by_base(cons(1, "a", nil), cons(1, "b", nil)))
        self.assertTrue(equal_by_base(keyword("k"), keyword("k")))


# The end.
