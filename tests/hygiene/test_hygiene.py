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
Tests for koskari.hygiene using synthetic Special-like objects.
"""


import unittest

import pytest

from koskari.exceptions import CompilerError
from koskari.hygiene import (
    Hygiene,
    apply_syntax_rules_hygiene,
    binder_recurse_targets,
    collect_binding_symbols,
    expand_template_with_hygiene,
    finalize_macro_hygiene_unbox_one_pass,
    introduced_symbols_from_match,
    scope_region_targets,
)
from koskari.lib import Datum, cons, nil, symbol
from koskari.types import build_proper, is_symbol


pytestmark = pytest.mark.phase_infra


class _FakeBinderOwner(object):


    def __init__(self, slot):
        self.slot = slot


    def iter_introduced_symbols(self, matched):
        val = matched.get(self.slot)
        if val is not None and is_symbol(val):
            yield val


    def iter_recurse_targets(self, matched):
        yield from ()


    def iter_scope_targets(self, matched):
        yield from ()


    def hygiene_wire(self):
        return ("DEFINE", self.slot, ())


class _FakeBindingRecurse(object):


    def iter_introduced_symbols(self, matched):
        yield from ()


    def iter_recurse_targets(self, matched):
        ex = matched.get("expr")
        if ex is not None:
            yield ex


    def iter_scope_targets(self, matched):
        yield from ()


    def hygiene_wire(self):
        return ("BINDING", "b", ())


class _FakeBodyScope(object):


    def iter_introduced_symbols(self, matched):
        yield from ()


    def iter_recurse_targets(self, matched):
        yield from ()


    def iter_scope_targets(self, matched):
        val = matched.get("body")
        if val is None:
            return
        yield val


    def hygiene_wire(self):
        return ("BODY", "body")


class _FakeCompiled(object):


    def __init__(self, hygiene, matched):
        self.hygiene = hygiene
        self._matched = matched


    def match_for_hygiene(self, rest):
        return self._matched


class TestHygieneHelpers(unittest.TestCase):


    def test_empty_hygiene_queries(self):
        """
        Hygiene defaults classify slots consistently
        """

        h = Hygiene({}, {})
        self.assertTrue(h.is_empty())
        self.assertFalse(h.is_binder("x"))
        self.assertFalse(h.is_scope_region("x"))


class TestIntroducedAndRecurse(unittest.TestCase):


    def test_introduced_symbols_define(self):
        """
        Binder owner introduces captures[slot] symbol
        """

        owner = _FakeBinderOwner("nm")
        hx = Hygiene({"nm": owner}, {})
        sn = symbol("n")
        matched = {"nm": sn}
        syms = introduced_symbols_from_match(matched, hx)
        self.assertEqual(syms, {sn})


    def test_binder_recurse_binding_targets_expr(self):
        """
        Owner recurse walks the expr capture when present
        """

        hx = Hygiene({"b": _FakeBindingRecurse()}, {})
        matched = {"expr": 42}
        got = list(binder_recurse_targets(matched, hx))
        self.assertEqual(got, [42])


class TestScopeRegionTargets(unittest.TestCase):


    def test_scope_region_yields_body_capture_once(self):
        """
        Scope-region owners normalize captures into walk roots for collect
        """

        bsym = symbol("b")
        matched = {"body": (bsym,)}
        roots = list(scope_region_targets(matched, Hygiene({}, {"body": _FakeBodyScope()})))
        self.assertEqual(len(roots), 1)
        self.assertIs(roots[0], bsym)


class TestExpandTemplate(unittest.TestCase):


    def test_non_pair_atom_passes_through(self):
        """
        Non-pair, non-symbol templates are opaque like Syntax boxes
        """

        sentinel = object()
        out = expand_template_with_hygiene(sentinel, {}, set())
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
        binding_symbols = set()
        with self.assertRaises(CompilerError) as ctx:
            expand_template_with_hygiene(tpl, bindings, binding_symbols)
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
        binding_symbols = set()
        out = expand_template_with_hygiene(tpl, bindings, binding_symbols)
        self.assertEqual(out, build_proper(plus, 10, 20))


    def test_template_ellipsis_list_binding_uses_repeat_count(self):
        """
        List-valued bindings keep indexed ellipsis repetition
        """

        n = symbol("n")
        tpl = build_proper(n, Ellipsis)
        bindings = {n: [11, 22]}
        binding_symbols = set()
        out = expand_template_with_hygiene(tpl, bindings, binding_symbols)
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
        binding_symbols = set()
        out = expand_template_with_hygiene(tpl, bindings, binding_symbols)
        want = cons(
            hash_list,
            cons(plus, 1, 2, nil),
            cons(plus, 3, 4, nil),
            cons(plus, 100, 200, nil),
            cons(plus, 5, -1, nil),
            nil,
        )
        self.assertEqual(out, want)


class TestCollectAndApply(unittest.TestCase):


    def test_collect_finds_define_introduced_symbol(self):
        """
        collect_binding_symbols walks Special-shaped heads via match_for_hygiene
        """

        hd = symbol("rule-head")
        owner = _FakeBinderOwner("nm")
        hx = Hygiene({"nm": owner}, {})
        intro = symbol("fresh")
        fake = _FakeCompiled(hx, {"nm": intro})
        table = {hd: fake}

        def find_compiled(head):
            return table.get(head)

        template = cons(hd, nil)
        syms = collect_binding_symbols(template, find_compiled)
        self.assertEqual(syms, {intro})


    def test_apply_syntax_rules_hygiene_round_trip(self):
        """
        apply_syntax_rules_hygiene expands introduced symbols using gensyms
        """

        hd = symbol("rule-head")
        owner = _FakeBinderOwner("nm")
        hx = Hygiene({"nm": owner}, {})
        intro = symbol("fresh")
        fake = _FakeCompiled(hx, {"nm": intro})
        table = {hd: fake}

        def find_compiled(head):
            return table.get(head)

        template = cons(hd, intro, nil)
        bindings = {}
        out = apply_syntax_rules_hygiene(template, bindings, find_compiled)
        parts = list(out.unpack())
        self.assertEqual(len(parts), 2)
        self.assertIs(parts[0], hd)
        self.assertNotEqual(parts[1], intro)


class TestCollectDatumOpaque(unittest.TestCase):


    def test_collect_does_not_harvest_inside_datum_on_tree(self):
        """
        ``Datum`` on the expanded tree is opaque for binding-symbol discovery
        """

        hd = symbol("compile/define")
        export = symbol("compile/caddddr")
        form = cons(hd, Datum(export), nil)
        syms = collect_binding_symbols(form, lambda head: None)
        self.assertNotIn(export, syms)


class TestFinalizeDatum(unittest.TestCase):


    def test_finalize_skips_gensym_for_datum_car(self):
        """
        Datum-wrapped substituted captures keep the real symbol after finalize
        """

        hd = symbol("compile/define")
        export = symbol("compile/caddddr")
        form = cons(hd, Datum(export), nil)
        out = finalize_macro_hygiene_unbox_one_pass(
            form, {export}, lambda head: None)
        self.assertIs(out.cdr.car, export)


    def test_finalize_gensyms_bare_introduced_symbol(self):
        """
        Bare template literals in binding_symbols are still freshened
        """

        hd = symbol("compile/define")
        export = symbol("compile/caddddr")
        form = cons(hd, export, nil)
        out = finalize_macro_hygiene_unbox_one_pass(
            form, {export}, lambda head: None)
        self.assertIsNot(out.cdr.car, export)


# The end.
