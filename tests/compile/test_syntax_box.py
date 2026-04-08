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

import pytest

from koskari.compile import Compiler
from koskari.compile.helpers import pycons
from koskari.compile.specials import SPECIALS
from koskari.lib import Module, ModuleNamespace, cdr, cons, nil, symbol
from koskari.matcher import MatchResult
from koskari.syntax import (MacroRulesTransformer, Syntax, box_match_capture,
                            build_macro_rules_transformer,
                            collect_pattern_variable_symbols)
from koskari.types import get_position, unpack


pytestmark = pytest.mark.phase_compile


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
        s = box_match_capture(mr)
        self.assertTrue(isinstance(s, Syntax))
        self.assertEqual(s.datum, 42)


    def test_collect_pattern_variables(self):
        lit = frozenset({symbol('m')})
        names = collect_pattern_variable_symbols(
            _ensure_form(('m', 'a', 'b', Ellipsis)),
            lit)
        self.assertEqual(names, [symbol('a'), symbol('b')])


    def test_syntax_iter_pair_yields_syntax_boxes(self):
        a, b = symbol('a'), symbol('b')
        p = cons(a, cons(b, nil))
        outer_pos = (1, 2)
        s = Syntax(p, outer_pos)
        out = list(iter(s))
        self.assertEqual(len(out), len(list(unpack(p))))
        self.assertEqual([x.datum for x in out], [a, b])
        self.assertEqual(
            [x.position for x in out],
            [get_position(p) or outer_pos, get_position(cdr(p)) or outer_pos])


    def test_syntax_iter_improper_matches_unpack(self):
        p = cons(1, 2)
        s = Syntax(p, (0, 0))
        self.assertEqual(
            [x.datum for x in s],
            list(unpack(p)))


    def test_syntax_iter_symbol_raises(self):
        s = Syntax(symbol('x'))
        with self.assertRaises(TypeError):
            list(iter(s))


    def test_syntax_iter_nil_yields_nothing(self):
        s = Syntax(nil)
        self.assertEqual(list(iter(s)), [])


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
