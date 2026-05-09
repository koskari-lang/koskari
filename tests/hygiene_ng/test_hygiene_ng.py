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
from koskari.lib import cons, keyword, nil, symbol
from koskari.matcher_ng import BODY, DEFINE, GROUP, HygienicMatchCompiler
from koskari.types import is_symbol


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
        Scope-region owners yield the body capture as a single root
        """

        bsym = symbol("b")
        matched = {"body": (bsym,)}
        roots = list(scope_region_targets(matched, Hygiene({}, {"body": _FakeBodyScope()})))
        self.assertEqual(len(roots), 1)
        self.assertEqual(roots[0], (bsym,))


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


class TestHygieneTupleWire(unittest.TestCase):


    def test_empty_hygiene_round_trip(self):
        """
        Empty Hygiene serializes to HYGIENE with empty binder and scope
        tuples
        """

        h = Hygiene({}, {})
        t = h.to_tuple()
        self.assertEqual(t, (keyword("HYGIENE"), (), ()))
        h2 = Hygiene.from_tuple(t)
        self.assertEqual(h2.to_tuple(), t)


    def test_compile_round_trip_define_body(self):
        """
        Hygiene from HygienicMatchCompiler.lower round-trips through tuple
        wire
        """

        pat = GROUP(DEFINE("name"), BODY("body"))
        hc = HygienicMatchCompiler()
        _frozen = hc.lower(pat)
        hygiene = hc.build_hygiene()
        t = hygiene.to_tuple()
        self.assertEqual(
            t,
            (keyword("HYGIENE"),
             (("DEFINE", "name", ()),),
             (("BODY", "body"),)))
        h2 = Hygiene.from_tuple(t)
        self.assertEqual(h2.to_tuple(), t)


    def test_from_tuple_rejects_bad_tag(self):
        """
        from_tuple requires the HYGIENE tag
        """

        with self.assertRaises(ValueError):
            Hygiene.from_tuple(("OTHER", (), ()))


# The end.
