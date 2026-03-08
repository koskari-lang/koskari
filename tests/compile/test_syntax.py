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
Tests for koskari.compile.syntax (SyntaxRulesTransformer, build_syntax).

Validate expansion output only, using transformers built via tagged tuples.

Helpers for test authors:
  form('head', arg, ...)     - Build a form; strings become symbols.
  rule(name, pat, tpl)       - Build transformer with one (pattern, template) rule.
  expand(transformer, form)  - Expand form through transformer.
  assert_forms_equal(tc, got, expected)
  assert_head(tc, form, symbol_name)
  assert_binding_renamed(tc, let_form, original_name)
  assert_formal_renamed(tc, lambda_form, original_name)
"""


import unittest

from koskari.compile import Compiler
from koskari.compile.helpers import pycons
from koskari.compile.specials import SPECIALS
from koskari.compile.syntax import (
    SyntaxRule,
    SyntaxRulesTransformer,
    build_syntax,
)
from koskari.lib import car, cdr, symbol
from koskari.types import Module, ModuleNamespace
from koskari.types import is_symbol, unpack


def _make_compiler():
    ns = ModuleNamespace('test', {})
    for sym, fn in SPECIALS.items():
        ns.store(str(sym), fn)
    mod = Module(ns)
    return Compiler('<test>', mod, filename=__file__)


def expand(transformer, input_form):
    """
    Expand input_form through transformer; returns expanded form
    """

    compiler = _make_compiler()
    return transformer.compile_impl(compiler, input_form, tc=False)


def _ensure_form(x):
    """
    Convert structure to form; strings become symbols
    """

    if isinstance(x, str):
        return symbol(x)
    if isinstance(x, (tuple, list)):
        return pycons(tuple(_ensure_form(v) for v in x))
    return x


def form(*parts):
    """
    Build a form. Strings become symbols: form('double', 21) -> (double 21)
    """

    return pycons(tuple(_ensure_form(p) for p in parts))


def rule(macro_name, pattern, template):
    """
    Build a SyntaxRulesTransformer. Pattern/template strings become symbols
    """

    return SyntaxRulesTransformer(
        symbol(macro_name),
        frozenset(),
        [SyntaxRule(_ensure_form(pattern), _ensure_form(template))],

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
    test_case.assertTrue(
        str(bound_name).startswith('syntax_'),
        'introduced binding must be gensym\'d')


def assert_formal_renamed(test_case, lambda_form, original_name):
    """
    Assert the first formal in a (compile/lambda FORMALS BODY) form was
    hygienically renamed from original_name.
    """

    formals = car(cdr(lambda_form))
    param = car(formals)
    test_case.assertTrue(is_symbol(param), 'formal must be symbol')
    test_case.assertNotEqual(str(param), original_name)
    test_case.assertTrue(
        str(param).startswith('syntax_'),
        'introduced formal must be gensym\'d')


class TestSyntaxExpansion(unittest.IsolatedAsyncioTestCase):

    async def test_simple_expansion_pattern_var_substituted(self):
        transformer = rule('double', ('double', 'x'), ('+', 'x', 'x'))
        expanded = expand(transformer, form('double', 21))
        expected = form('+', 21, 21)
        assert_forms_equal(self, expanded, expected)


    async def test_let_hygiene_introduced_symbol_renamed(self):
        transformer = rule(
            'use-temp',
            ('use-temp', 'x'),
            ('let', (('t', 99),), ('+', 't', 'x')),
        )
        expanded = expand(transformer, form('use-temp', 5))
        assert_head(self, expanded, 'let')
        assert_binding_renamed(self, expanded, 't')


    async def test_lambda_hygiene_introduced_param_renamed(self):
        transformer = rule(
            'lambda-inc',
            ('lambda-inc',),
            ('compile/lambda', ('n',), ('+', 'n', 1)),
        )
        expanded = expand(transformer, form('lambda-inc'))
        assert_head(self, expanded, 'compile/lambda')
        assert_formal_renamed(self, expanded, 'n')


    async def test_build_syntax_from_tagged_tuple_roundtrip(self):
        transformer = rule('wrap', ('wrap', 'x'), ('list', 'x'))
        tagged = transformer.to_tuple()
        rebuilt = build_syntax(symbol('wrap'), tagged)
        expanded = expand(rebuilt, form('wrap', 99))
        expected = form('list', 99)
        assert_forms_equal(self, expanded, expected)


# The end.
