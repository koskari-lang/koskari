# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for matcher Syntax boxing and macro-rules expansion.
"""

import unittest

from koskari.compile import Compiler
from koskari.compile.helpers import pycons
from koskari.compile.macro_rules import MacroRulesTransformer, build_macro_rules_transformer
from koskari.compile.specials import SPECIALS
from koskari.compile.syntax import collect_pattern_variable_symbols
from koskari.lib import symbol
from koskari.matcher import MatchResult, Syntax
from koskari.types import Module, ModuleNamespace

import pytest

pytestmark = pytest.mark.phase_compile


def _sym(s):
    return symbol(s)


def _make_compiler():
    ns = ModuleNamespace('test', {})
    for sym, fn in SPECIALS.items():
        ns.store(str(sym), fn)

    mod = Module(ns)
    return Compiler('<test>', mod, filename=__file__)


def _ensure_form(x):
    if isinstance(x, str):
        return symbol(x)
    if isinstance(x, (tuple, list)):
        return pycons(tuple(_ensure_form(v) for v in x))
    return x


def form(*parts):
    return pycons(tuple(_ensure_form(p) for p in parts))


class TestSyntaxBox(unittest.TestCase):

    def test_match_result_box(self):
        mr = MatchResult(42, None)
        s = mr.box()
        self.assertTrue(isinstance(s, Syntax))
        self.assertEqual(s.datum, 42)

    def test_collect_pattern_variables(self):
        lit = frozenset({_sym('m')})
        names = collect_pattern_variable_symbols(
            _ensure_form(('m', 'a', 'b', Ellipsis)),
            lit)
        self.assertEqual(names, [_sym('a'), _sym('b')])


class TestMacroRules(unittest.TestCase):

    def test_macro_rules_identity_returns_syntax(self):
        compiler = _make_compiler()
        spec = form(
            'macro-rules',
            (),
            form(
                form('double', 'x'),
                'x',
            ),
        )
        tr = build_macro_rules_transformer(
            compiler, symbol('double'), spec)
        out = tr.compile_impl(
            compiler, form('double', 21), frozenset({symbol('double')}))
        self.assertTrue(isinstance(out, Syntax))
        self.assertEqual(out.datum, 21)

    def test_macro_rules_tuple_roundtrip(self):
        compiler = _make_compiler()
        spec = form(
            'macro-rules',
            (),
            form(
                form('double', 'x'),
                'x',
            ),
        )
        tr = build_macro_rules_transformer(
            compiler, symbol('double'), spec)
        tr2 = MacroRulesTransformer.from_tuple(tr.to_tuple())
        out = tr2.compile_impl(
            compiler, form('double', 21), frozenset({symbol('double')}))
        self.assertTrue(isinstance(out, Syntax))
        self.assertEqual(out.datum, 21)


# The end.
