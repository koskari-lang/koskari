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
Tests for syntax expansion and pattern lowering.

Runtime types and ``build_syntax_rules`` come from ``koskari.syntax_ng``.
``build_syntax_rules_transformer`` and ``syntax_pattern_to_matcher`` are
compile-time helpers in ``koskari.compile.specials.syntax``.

Validate expansion output only, using transformers built via tagged tuples.
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.compile.specials import BUILTINS
from koskari.compile.specials.syntax import (build_syntax_rules_transformer,
                                             syntax_pattern_to_matcher)
from koskari.lib import Module, ModuleNamespace, car, cdr, cons, nil, symbol
from koskari.syntax_ng import Syntax, SyntaxTransform, build_syntax_rules
from koskari.types import build_proper, is_symbol, unpack


pytestmark = pytest.mark.phase_compile


def _make_compiler():
    ns = ModuleNamespace('test', {})
    for sym, fn in BUILTINS.items():
        ns.store(str(sym), fn)
    mod = Module(ns)
    return Compiler('<test>', mod, filename=__file__)


def expand(transformer, input_form):
    """
    Expand input_form through transformer; returns expanded form
    """

    compiler = _make_compiler()
    return transformer.compile_impl(compiler, input_form, tc=False)


def rule(macro_name, pat_form, tpl_form):
    """
    Build a Syntax from pattern and template cons trees.
    """

    name = symbol(macro_name)
    literals = frozenset({name})
    pat_m = syntax_pattern_to_matcher(pat_form, literals, pat_form)

    return Syntax(
        name,
        (),
        (SyntaxTransform(pat_m, tpl_form),),
        serialized_literals=literals,
    )


def assert_forms_equal(test_case, got, expected):
    """
    Assert two forms are equal (compare as unpacked lists)
    """

    test_case.assertEqual(list(unpack(got)), list(unpack(expected)))


def assert_head(test_case, form_pair, expected_symbol):
    """
    Assert car(form_pair) equals expected_symbol
    """

    test_case.assertEqual(car(form_pair), symbol(expected_symbol))


def assert_binding_renamed(test_case, let_form, original_name):
    """
    Assert the first binding in a (let BINDINGS BODY) form was hygienically
    renamed from original_name.
    """

    bindings = car(cdr(let_form))
    binding_pair = car(bindings)
    bound_name = car(binding_pair)
    test_case.assertTrue(is_symbol(bound_name), 'binding name must be symbol')
    test_case.assertNotEqual(str(bound_name), original_name)


def assert_formal_renamed(test_case, lambda_form, original_name):
    """
    Assert the first formal in a (compile/lambda FORMALS BODY) form was
    hygienically renamed from original_name.
    """

    formals = car(cdr(lambda_form))
    param = car(formals)
    test_case.assertTrue(is_symbol(param), 'formal must be symbol')
    test_case.assertNotEqual(str(param), original_name)


class TestSyntaxExpansion(unittest.TestCase):

    def test_simple_expansion_pattern_var_substituted(self):
        pat = build_proper(symbol('double'), symbol('x'))
        tpl = build_proper(symbol('+'), symbol('x'), symbol('x'))
        transformer = rule('double', pat, tpl)
        expanded = expand(transformer, build_proper(symbol('double'), 21))
        expected = build_proper(symbol('+'), 21, 21)
        assert_forms_equal(self, expanded, expected)


    def test_let_hygiene_introduced_symbol_renamed(self):
        bindings = build_proper(build_proper(symbol('t'), 99))
        let_tpl = build_proper(
            symbol('let'),
            bindings,
            build_proper(symbol('+'), symbol('t'), symbol('x')),
        )
        transformer = rule(
            'use-temp',
            build_proper(symbol('use-temp'), symbol('x')),
            let_tpl,
        )
        expanded = expand(transformer, build_proper(symbol('use-temp'), 5))
        assert_head(self, expanded, 'let')
        assert_binding_renamed(self, expanded, 't')


    def test_lambda_hygiene_introduced_param_renamed(self):
        transformer = rule(
            'lambda-inc',
            build_proper(symbol('lambda-inc')),
            build_proper(
                symbol('compile/lambda'),
                build_proper(symbol('n')),
                build_proper(symbol('+'), symbol('n'), 1),
            ),
        )
        expanded = expand(transformer, build_proper(symbol('lambda-inc')))
        assert_head(self, expanded, 'compile/lambda')
        assert_formal_renamed(self, expanded, 'n')


    def test_build_syntax_from_tagged_tuple_roundtrip(self):
        transformer = rule(
            'wrap',
            build_proper(symbol('wrap'), symbol('x')),
            build_proper(symbol('list'), symbol('x')),
        )
        tagged = transformer.to_syntax_rules_tagged()
        rebuilt = build_syntax_rules(symbol('wrap'), tagged)
        expanded = expand(rebuilt, build_proper(symbol('wrap'), 99))
        expected = build_proper(symbol('list'), 99)
        assert_forms_equal(self, expanded, expected)


class TestSyntaxRulesPatternParsing(unittest.TestCase):

    def test_match_pattern_equivalent_to_plain(self):
        rule_plain = build_proper(
            build_proper(symbol('double'), symbol('x')),
            build_proper(symbol('+'), symbol('x'), symbol('x')),
        )
        rule_match = build_proper(
            build_proper(
                symbol('match'),
                build_proper(symbol('double'), symbol('x')),
            ),
            build_proper(symbol('+'), symbol('x'), symbol('x')),
        )
        t_plain = build_syntax_rules_transformer(
            symbol('double'), nil, cons(rule_plain, nil))
        t_match = build_syntax_rules_transformer(
            symbol('double'), nil, cons(rule_match, nil))
        ex = expand(t_plain, build_proper(symbol('double'), 21))
        assert_forms_equal(
            self, ex, expand(t_match, build_proper(symbol('double'), 21)))


    def test_rule_pattern_explicit_value_matcher(self):
        rule_row = build_proper(
            build_proper(
                symbol('double'),
                build_proper(symbol('VALUE'), symbol('x')),
            ),
            build_proper(symbol('+'), symbol('x'), symbol('x')),
        )
        t = build_syntax_rules_transformer(
            symbol('double'), nil, cons(rule_row, nil), match_patterns=True)
        ex = expand(t, build_proper(symbol('double'), 21))
        assert_forms_equal(self, ex, build_proper(symbol('+'), 21, 21))


    def test_syntax_rule_matcher_roundtrip(self):
        name = symbol('wrap')
        literals = frozenset({name})
        pat_form = build_proper(symbol('wrap'), symbol('x'))
        tpl = build_proper(symbol('list'), symbol('x'))
        pat_m = syntax_pattern_to_matcher(pat_form, literals, pat_form)
        syn = Syntax(
            name,
            (),
            (SyntaxTransform(pat_m, tpl),),
            serialized_literals=literals,
        )
        tagged = syn.to_syntax_rules_tagged()
        syn2 = build_syntax_rules(name, tagged)
        expanded = expand(syn2, build_proper(symbol('wrap'), 99))
        assert_forms_equal(self, expanded, build_proper(symbol('list'), 99))


    def test_single_var_pattern_binds_whole_form(self):
        transformer = rule(
            'capture',
            symbol('x'),
            build_proper(symbol('list'), symbol('x')),
        )
        expanded = expand(
            transformer,
            build_proper(symbol('capture'), 1, 2, 3))
        assert_forms_equal(
            self,
            expanded,
            build_proper(symbol('list'), build_proper(symbol('capture'), 1, 2, 3)),
        )


    def test_ellipsis_zero_or_more(self):
        transformer = rule(
            'take',
            build_proper(symbol('take'), symbol('x'), Ellipsis),
            build_proper(symbol('list'), symbol('x'), Ellipsis),
        )
        expanded = expand(transformer, build_proper(symbol('take'), 1, 2))
        assert_forms_equal(self, expanded, build_proper(symbol('list'), 1, 2))
        expanded0 = expand(transformer, build_proper(symbol('take')))
        assert_forms_equal(self, expanded0, build_proper(symbol('list')))


# The end.
