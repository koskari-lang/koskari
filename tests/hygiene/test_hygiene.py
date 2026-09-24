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
    equal_by_base, expand_template_with_hygiene, finalize_expansion,
    mark_expansion)
from koskari.lib import (
    Datum, bracket_cons, bracket_nil, bracket_pair, cons, gensym, keyword,
    kos_repr, nil, pair, symbol)
from koskari.types import build_proper, get_position, unpack


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


class TestDatumqDirective(unittest.TestCase):
    """
    A template pair headed by the raw datumq symbol emits its one form
    raw, in a Datum, unmarked and with no pattern variable substituted;
    the Datum carries the position of the spine cell holding the form
    """

    DATUMQ = symbol("datumq")


    def test_directive_emits_unmarked_datum(self):
        foo = symbol("foo")
        tpl = build_proper(symbol("list"), build_proper(self.DATUMQ, foo))
        out = expand_template_with_hygiene(tpl, {}, 7)
        head, rest = out
        self.assertIsNot(head.base, head)
        self.assertIsInstance(rest.car, Datum)
        self.assertIs(rest.car.datum, foo)


    def test_pattern_variable_inside_is_not_substituted(self):
        foo = symbol("foo")
        tpl = build_proper(self.DATUMQ, foo)
        out = expand_template_with_hygiene(tpl, {foo: 5}, 7)
        self.assertIsInstance(out, Datum)
        self.assertIs(out.datum, foo)


    def test_position_is_the_spine_cell_holding_the_form(self):
        foo = symbol("foo")
        directive = build_proper(self.DATUMQ, foo)
        directive.cdr.set_position((3, 7))
        tpl = build_proper(symbol("list"), directive)
        out = expand_template_with_hygiene(tpl, {}, 7)
        self.assertEqual(out.cdr.car.position, (3, 7))

        # the peel moves the position onto the cell that now holds foo
        final = finalize_expansion(out)
        self.assertIs(final.cdr.car, foo)
        self.assertEqual(get_position(final.cdr), (3, 7))


    def test_pair_form_keeps_the_spine_cell_position(self):
        inner = build_proper(symbol("a"), symbol("b"))
        directive = build_proper(self.DATUMQ, inner)
        directive.cdr.set_position((4, 2))
        out = expand_template_with_hygiene(directive, {}, 7)
        self.assertIs(out.datum, inner)
        self.assertEqual(out.position, (4, 2))


    def test_arity_other_than_one_is_refused(self):
        for tpl in (build_proper(self.DATUMQ),
                    build_proper(self.DATUMQ, symbol("a"), 3)):
            with self.assertRaises(CompilerError) as caught:
                expand_template_with_hygiene(tpl, {}, 7)
            self.assertIn("exactly one form", str(caught.exception))


    def test_literal_datumq_call_escape(self):
        foo = symbol("foo")
        tpl = build_proper(build_proper(self.DATUMQ, self.DATUMQ), foo)
        final = finalize_expansion(expand_template_with_hygiene(tpl, {}, 7))
        head, rest = final
        self.assertIs(head, self.DATUMQ)
        self.assertIs(rest.car.base, foo)
        self.assertIsNot(rest.car, foo)


    def test_nested_directive_emits_the_inner_one_raw(self):
        foo = symbol("foo")
        inner = build_proper(self.DATUMQ, foo)
        tpl = build_proper(self.DATUMQ, inner)
        out = expand_template_with_hygiene(tpl, {}, 7)
        self.assertIs(out.datum, inner)


    def test_directive_body_does_not_drive_an_ellipsis(self):
        x = symbol("x")
        tag = symbol("tag")
        tpl = build_proper(
            build_proper(symbol("cons"), x, build_proper(self.DATUMQ, tag)),
            Ellipsis)
        final = finalize_expansion(
            expand_template_with_hygiene(tpl, {x: [1, 2], tag: [7, 8, 9]}, 7))
        rows = list(unpack(final))
        self.assertEqual(len(rows), 2)
        self.assertEqual([r.cdr.car for r in rows], [1, 2])
        self.assertEqual([r.cdr.cdr.car for r in rows], [tag, tag])


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
