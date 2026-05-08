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
Tests for koskari.hygiene_ng using synthetic Special-like objects.
"""


import unittest

import pytest

from koskari.exceptions import CompilerError
from koskari.hygiene_ng import (
    Hygiene,
    apply_syntax_rules_hygiene,
    binder_recurse_targets,
    collect_binding_symbols,
    expand_template_with_hygiene,
    introduced_symbols_from_match,
    scope_region_targets,
)
from koskari.lib import cons, nil, symbol


pytestmark = pytest.mark.phase_infra


class _FakeCompiled(object):


    def __init__(self, hygiene, matched):
        self.hygiene = hygiene
        self._matched = matched


    def match_for_hygiene(self, rest):
        return self._matched


class TestHygieneHelpers(unittest.TestCase):


    def test_empty_hygiene_queries(self):
        """
        Hygiene defaults classify slots and kinds consistently
        """

        h = Hygiene({}, {})
        self.assertTrue(h.is_empty())
        self.assertFalse(h.is_binder("x"))
        self.assertEqual(h.binder_kinds(), {})


    def test_binder_kinds_maps_slots(self):
        """
        binder_kinds exposes declared binder kinds by slot name
        """

        h = Hygiene({"n": {"kind": "DEFINE", "scopes": ()}}, {})
        self.assertFalse(h.is_empty())
        self.assertTrue(h.is_binder("n"))
        self.assertEqual(h.binder_kinds(), {"n": "DEFINE"})


class TestIntroducedAndRecurse(unittest.TestCase):


    def test_introduced_symbols_define(self):
        """
        DEFINE binder introduces captures[slot] symbol
        """

        hx = Hygiene({"nm": {"kind": "DEFINE", "scopes": ()}}, {})
        sn = symbol("n")
        matched = {"nm": sn}
        syms = introduced_symbols_from_match(matched, hx)
        self.assertEqual(syms, {sn})


    def test_binder_recurse_binding_targets_expr(self):
        """
        BINDING recurse walks the expr capture when present
        """

        hx = Hygiene({"b": {"kind": "BINDING", "scopes": ()}}, {})
        matched = {"expr": 42}
        got = list(binder_recurse_targets(matched, hx))
        self.assertEqual(got, [42])


class TestScopeRegionTargets(unittest.TestCase):


    def test_scope_region_yields_body_tuple_elements(self):
        """
        Scope-region slots configured on hygiene yield walk roots for each
        tuple element
        """

        hx = Hygiene({}, {"body": {"kind": "REST"}})
        bsym = symbol("b")
        matched = {"body": (bsym,)}
        roots = list(scope_region_targets(matched, hx))
        self.assertEqual(len(roots), 1)
        self.assertEqual(roots[0], cons(bsym, nil))


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


class TestCollectAndApply(unittest.TestCase):


    def test_collect_finds_define_introduced_symbol(self):
        """
        collect_binding_symbols walks Special-shaped heads via match_for_hygiene
        """

        hd = symbol("rule-head")
        hx = Hygiene({"nm": {"kind": "DEFINE", "scopes": ()}}, {})
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
        hx = Hygiene({"nm": {"kind": "DEFINE", "scopes": ()}}, {})
        intro = symbol("fresh")
        fake = _FakeCompiled(hx, {"nm": intro})
        table = {hd: fake}

        def find_compiled(head):
            return table.get(head)

        template = cons(hd, cons(intro, nil))
        bindings = {}
        out = apply_syntax_rules_hygiene(template, bindings, find_compiled)
        parts = list(out.unpack())
        self.assertEqual(len(parts), 2)
        self.assertIs(parts[0], hd)
        self.assertNotEqual(parts[1], intro)


# The end.
