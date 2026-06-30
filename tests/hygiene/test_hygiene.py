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
    walk_hygiene_tree,
)
from koskari.lib import (Datum, bracket_nil, bracket_pair, cons, kos_repr, nil,
                         pair, symbol)
from koskari.types import build_proper, is_pair, is_symbol


pytestmark = pytest.mark.phase_infra


class _FakeBinderOwner(object):


    def __init__(self, slot):
        self.slot = slot


    def iter_introduced_symbols(self, matched):
        val = matched.get(self.slot)
        if val is None:
            return
        if isinstance(val, Datum) and val.from_datum:
            return
        if isinstance(val, Datum):
            val = val.datum
        if is_symbol(val):
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
        return self._matched, None, self.hygiene


class TestHygieneHelpers(unittest.TestCase):


    def test_empty_hygiene_queries(self):
        """
        Hygiene defaults classify slots consistently
        """

        h = Hygiene.single_node()
        self.assertTrue(h.is_empty())
        root = h.root
        self.assertNotIn("x", root.binders)
        self.assertNotIn("x", root.regions)


class TestIntroducedAndRecurse(unittest.TestCase):


    def test_introduced_symbols_define(self):
        """
        Binder owner introduces captures[slot] symbol
        """

        owner = _FakeBinderOwner("nm")
        hx = Hygiene.single_node(binders={"nm": owner})
        sn = symbol("n")
        matched = {"nm": Datum(sn)}
        syms = introduced_symbols_from_match(matched, hx)
        self.assertEqual(syms, {sn})


    def test_introduced_symbols_skips_from_datum_capture(self):
        """
        Substitution captures with ``from_datum`` do not enter binding_symbols
        """

        owner = _FakeBinderOwner("nm")
        hx = Hygiene.single_node(binders={"nm": owner})
        sn = symbol("n")
        matched = {"nm": Datum(sn, from_datum=True)}
        syms = introduced_symbols_from_match(matched, hx)
        self.assertEqual(syms, set())


    def test_binder_recurse_binding_targets_expr(self):
        """
        Owner recurse walks the expr capture when present
        """

        hx = Hygiene.single_node(binders={"b": _FakeBindingRecurse()})
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
        tree = Hygiene.single_node(regions={"body": _FakeBodyScope()})
        tracer = {}
        walk_hygiene_tree(
            tree, tree.root_id, matched, lambda h: None, tracer)
        self.assertEqual(tracer.get("region_walk"), ["body"])


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


    def test_bracket_template_preserves_brackets(self):
        """a [ ] template expands to a bracket list, not a flattened paren one"""

        # template: (compile/cond [condition then]) -- the `if` macro shape
        cond = symbol("condition")
        then = symbol("then")
        inner = bracket_pair(cond, bracket_pair(then, bracket_nil))
        tpl = build_proper(symbol("compile/cond"), inner)
        out = expand_template_with_hygiene(
            tpl, {cond: symbol("foo"), then: 100}, set())

        self.assertIs(type(out), pair)            # outer form stays paren
        inner_out = out.cdr.car
        self.assertIs(type(inner_out), bracket_pair)   # inner stays bracket
        self.assertEqual(kos_repr(out), "(compile/cond [foo 100])")


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
        hx = Hygiene.single_node(binders={"nm": owner})
        intro = symbol("fresh")
        fake = _FakeCompiled(hx, {"nm": intro})
        table = {hd: fake}

        def find_compiled(head):
            return table.get(head)

        template = cons(hd, nil)
        syms = collect_binding_symbols(template, find_compiled)
        self.assertEqual(syms, {intro})


    def test_collect_sibling_form_list_includes_car_form(self):
        """
        A proper list of peer forms must collect binders from the first form,
        not only from the cdr (layout blocks and macro bodies use this shape)
        """

        hd1 = symbol("peer-one")
        hd2 = symbol("peer-two")
        owner1 = _FakeBinderOwner("a")
        owner2 = _FakeBinderOwner("b")
        hx1 = Hygiene.single_node(binders={"a": owner1})
        hx2 = Hygiene.single_node(binders={"b": owner2})
        intro1 = symbol("x")
        intro2 = symbol("y")
        fake1 = _FakeCompiled(hx1, {"a": intro1})
        fake2 = _FakeCompiled(hx2, {"b": intro2})
        table = {hd1: fake1, hd2: fake2}

        def find_compiled(head):
            return table.get(head)

        first_form = cons(hd1, nil)
        second_form = cons(hd2, nil)
        peer_list = cons(first_form, second_form, nil)
        syms = collect_binding_symbols(peer_list, find_compiled)
        self.assertEqual(syms, {intro1, intro2})


    def test_apply_syntax_rules_hygiene_round_trip(self):
        """
        apply_syntax_rules_hygiene expands introduced symbols using gensyms
        """

        hd = symbol("rule-head")
        owner = _FakeBinderOwner("nm")
        hx = Hygiene.single_node(binders={"nm": owner})
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


def _make_hygiene_compiler():
    from koskari.compile import Compiler
    from koskari.bootstrap import _BUILTIN_REGISTRY
    from koskari.lib import Module, ModuleNamespace

    mod_ns = ModuleNamespace("test", {})
    for sym, fn in _BUILTIN_REGISTRY.items():
        mod_ns.store(str(sym), fn)
    module = Module(mod_ns)
    return Compiler("<test>", module, filename=__file__)


def _bootstrap_define_syntax_macro_match_form(macro_name, inner_name):
    """
    Post-define-syntax shape for (define-syntax NAME (macro-match ...))
    """

    pattern = cons(macro_name, cons(inner_name, nil), nil)
    rule = cons(pattern, cons(nil, nil), nil)
    macro_match = cons(
        symbol("compile/macro-match"),
        Datum(macro_name),
        cons(rule, nil),
        nil)
    return cons(
        symbol("compile/define"),
        Datum(macro_name),
        macro_match,
        nil)


class TestDefineSyntaxBootstrapHygiene(unittest.TestCase):


    def test_collect_skips_datum_substitution_export_name(self):
        """
        define-syntax ``name`` in a ``Datum`` must not enter binding_symbols
        """

        macro_name = symbol("foo")
        inner = symbol("inner")
        form = _bootstrap_define_syntax_macro_match_form(macro_name, inner)
        compiler = _make_hygiene_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)
        self.assertNotIn(macro_name, syms)


    def test_finalize_preserves_pattern_literal_matching_macro_name(self):
        """
        Pattern head literal must not be gensymmed when export is Datum-only
        """

        macro_name = symbol("foo")
        inner = symbol("inner")
        form = _bootstrap_define_syntax_macro_match_form(macro_name, inner)
        compiler = _make_hygiene_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)
        macro_match = form.cdr.cdr.car
        rule = macro_match.cdr.cdr.car.car
        pat = rule.car
        while isinstance(pat, Datum):
            pat = pat.datum
        self.assertIs(pat.car, macro_name)
        finalize_macro_hygiene_unbox_one_pass(
            form, syms, compiler.find_compiled)
        pat = rule.car
        while isinstance(pat, Datum):
            pat = pat.datum
        self.assertIs(pat.car, macro_name)


    def test_collect_includes_bare_define_export_name(self):
        """
        Template (compile/define loop ...) still introes bare export names
        """

        loop = symbol("loop")
        form = cons(
            symbol("compile/define"),
            loop,
            cons(nil, nil),
            nil)
        compiler = _make_hygiene_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)
        self.assertIn(loop, syms)


    def test_collect_define_value_descent_finds_let_binder(self):
        """
        VALUE capture descent finds nested let binders; Datum exports do not
        """

        tp = symbol("tp")
        export = symbol("compile/MyType?")
        inner_type = symbol("compile/MyType")
        bindings = cons(cons(cons(tp, Datum(inner_type), nil), nil), nil)
        body = cons(
            symbol("compile/lambda"),
            Datum(export),
            cons(symbol("value"), nil),
            nil)
        let_form = cons(symbol("compile/let"), bindings, body, nil)
        form = cons(symbol("compile/define"), Datum(export), let_form, nil)
        compiler = _make_hygiene_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)
        self.assertIn(tp, syms)
        self.assertNotIn(export, syms)
        self.assertNotIn(inner_type, syms)


    def test_finalize_gensyms_template_let_binder_tp(self):
        """
        Template ``tp`` in create-instance-check shape must be gensymmed
        """

        tp = symbol("tp")
        export = symbol("foobar?")
        type_sym = symbol("foobar")
        bindings = cons(cons(cons(tp, Datum(type_sym), nil), nil), nil)
        isinstance_form = cons(
            symbol("compile/isinstance"),
            symbol("value"),
            tp,
            nil)
        lambda_form = cons(
            symbol("compile/lambda"),
            export,
            cons(cons(symbol("value"), nil), nil),
            isinstance_form,
            nil)
        let_form = cons(symbol("compile/let"), bindings, lambda_form, nil)
        form = cons(symbol("compile/define"), Datum(export), let_form, nil)
        compiler = _make_hygiene_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)
        self.assertIn(tp, syms)
        finalize_macro_hygiene_unbox_one_pass(
            form, syms, compiler.find_compiled)

        found = set()

        def walk(node):
            if is_symbol(node):
                found.add(node)
                return
            if isinstance(node, Datum):
                walk(node.datum)
                return
            if is_pair(node):
                for elem in node.unpack():
                    walk(elem)

        walk(form)
        self.assertNotIn(tp, found)


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
