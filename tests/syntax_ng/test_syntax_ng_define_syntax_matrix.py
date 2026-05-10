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
Matrix tests for the six Ki ``define-syntax`` shapes as ``syntax_ng``
builders (RULE vs MATCH times syntax / macro / special), plus
pattern-language regression spikes. No legacy ``koskari.syntax`` or
``koskari.matcher`` imports.
"""


import unittest

import pytest

from koskari.lib import cons, nil, symbol
from koskari.matcher_ng import parse_explicit_rule_pattern
from koskari.syntax_ng import (build_macro_match, build_macro_syntax,
                               build_special_match, build_special_syntax,
                               build_syntax, build_syntax_match)
from koskari.types import build_proper


pytestmark = pytest.mark.phase_infra


class _StubCompiler(object):


    def find_compiled(self, sym):
        return None


class TestDefineSyntaxMatrix(unittest.TestCase):


    def test_syntax_rules_shape(self):
        """
        syntax-rules analogue: RULE pattern, explicit literals list
        """

        m = symbol("compile/matrix-sr")
        onlyhere = symbol("onlyhere")
        rules = (
            (build_proper(m, onlyhere), symbol("narrow")),
            (build_proper(m, symbol("a"), symbol("b")), symbol("wide")),
        )
        s = build_syntax(m, (onlyhere,), rules)
        comp = _StubCompiler()
        src = cons(m, symbol("solo"), symbol("extra"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("wide"))


    def test_syntax_match_shape(self):
        """
        syntax-match analogue: MATCH-dialect pattern column
        """

        m = symbol("compile/matrix-sm")
        pat = build_proper(
            m, build_proper(symbol("SYMBOL"), symbol("u")))
        rules = ((pat, symbol("syntax-match-out")),)
        s = build_syntax_match(m, rules)
        comp = _StubCompiler()
        src = cons(m, symbol("arg"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("syntax-match-out"))


    def test_macro_rules_shape(self):
        """
        macro-rules analogue: RULE pattern, procedural arm
        """

        m = symbol("compile/matrix-mr")
        saw = []

        def proc(compiler, tc=None, **caps):
            saw.append((tc, caps))
            return symbol("macro-rules-out")

        s = build_macro_syntax(m, (), ((build_proper(m, symbol("v")), proc),))
        comp = _StubCompiler()
        slot = symbol("slotval")
        src = cons(m, slot, nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("macro-rules-out"))
        self.assertEqual(saw[0][1]["v"].datum, slot)


    def test_macro_match_shape(self):
        """
        macro-match analogue: MATCH pattern, same use-site as macro-rules
        """

        m = symbol("compile/matrix-mm")
        pat = build_proper(
            m, build_proper(symbol("SYMBOL"), symbol("v")))
        saw = []

        def proc(compiler, tc=None, **caps):
            saw.append((tc, caps))
            return symbol("macro-match-out")

        s = build_macro_match(m, ((pat, proc),))
        comp = _StubCompiler()
        slot = symbol("slotval")
        src = cons(m, slot, nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("macro-match-out"))
        self.assertEqual(saw[0][1]["v"].datum, slot)


    def test_special_rules_shape(self):
        """
        special-rules analogue: RULE pattern, special handler
        """

        m = symbol("compile/matrix-spr")

        def proc(compiler, source_obj, result, tc):
            return symbol("special-rules-out")

        s = build_special_syntax(
            m, (), ((build_proper(m, symbol("q")), proc),))
        src = cons(m, symbol("arg"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("special-rules-out"))


    def test_special_match_shape(self):
        """
        special-match analogue: MATCH pattern, same handler contract
        """

        m = symbol("compile/matrix-spm")
        pat = build_proper(
            m, build_proper(symbol("SYMBOL"), symbol("q")))

        def proc(compiler, source_obj, result, tc):
            return symbol("special-match-out")

        s = build_special_match(m, ((pat, proc),))
        src = cons(m, symbol("arg"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("special-match-out"))


class TestPatternLanguageTraps(unittest.TestCase):


    def test_rule_ellipsis_pattern_builds_and_matches(self):
        """
        RULE dialect ellipsis in ``build_syntax`` is accepted
        """

        m = symbol("compile/trap-ell")
        a = symbol("a")
        body_pat = build_proper(m, a, Ellipsis)
        rules = ((body_pat, symbol("ellipsis-ok")),)
        s = build_syntax(m, (), rules)
        comp = _StubCompiler()
        src = build_proper(m, symbol("x"), symbol("y"))
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("ellipsis-ok"))


    def test_match_dialect_rejects_same_ellipsis_spine(self):
        """
        MATCH dialect rejects ``Ellipsis`` in the same rule spine shape
        """

        m = symbol("compile/trap-ell2")
        a = symbol("a")
        body_pat = build_proper(m, a, Ellipsis)
        rules = ((body_pat, symbol("bad")),)
        with self.assertRaises(ValueError) as ar:
            build_syntax_match(m, rules)
        self.assertIn("ellipsis", str(ar.exception).lower())


    def test_group_pattern_requires_explicit_builder(self):
        """
        A ``GROUP`` head is meaningful under ``parse_explicit_rule_pattern``;
        ``build_syntax_match`` can use it
        """

        m = symbol("compile/trap-grp")
        inner = build_proper(
            symbol("GROUP"),
            symbol("g"),
            build_proper(symbol("SYMBOL"), symbol("w")))
        pat = build_proper(m, inner)
        rules = ((pat, symbol("group-out")),)
        s = build_syntax_match(m, rules)
        comp = _StubCompiler()
        src = cons(m, symbol("inner-form"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("group-out"))


    def test_top_level_ellipsis_in_parse_explicit(self):
        """
        ``parse_explicit_rule_pattern(Ellipsis)`` raises
        """

        with self.assertRaises(ValueError) as ar:
            parse_explicit_rule_pattern(Ellipsis)
        self.assertIn("ellipsis", str(ar.exception).lower())


# The end.
