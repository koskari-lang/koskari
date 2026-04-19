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
Tests for matcher Syntax boxing and macro-rules parse shape.
"""

import unittest

import pytest

from koskari.lib import car, cdr, cons, nil, symbol
from koskari.matcher import MatchResult
from koskari.syntax import (Syntax, box_match_capture,
                            collect_pattern_variable_symbols,
                            unbox_macro, unbox_macro_with_hygiene)
from koskari.types import get_position, is_nil, is_pair, unpack


pytestmark = pytest.mark.phase_compile


def _tree_contains_syntax(form):
    if isinstance(form, Syntax):
        return True

    if is_nil(form):
        return False

    if is_pair(form):
        return _tree_contains_syntax(car(form)) or _tree_contains_syntax(
            cdr(form))

    return False


class TestSyntaxBox(unittest.TestCase):

    def test_match_result_box(self):
        mr = MatchResult(42, None)
        s = box_match_capture(mr)
        self.assertTrue(isinstance(s, Syntax))
        self.assertEqual(s.datum, 42)


    def test_collect_pattern_variables(self):
        lit = frozenset({symbol('m')})
        pat = cons(symbol('m'), symbol('a'), symbol('b'), Ellipsis, nil)
        names = collect_pattern_variable_symbols(pat, lit)
        self.assertEqual(names, [symbol('a'), symbol('b')])


    def test_syntax_iter_pair_yields_syntax_boxes(self):
        a, b = symbol('a'), symbol('b')
        p = cons(a, b, nil)
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


    def test_unbox_macro_with_hygiene_strips_syntax(self):
        y = symbol('y')
        boxed = Syntax(cons(y, nil), (1, 1))
        form = cons(symbol('list'), boxed, nil)
        out = unbox_macro_with_hygiene(form, lambda _h: None)
        self.assertFalse(_tree_contains_syntax(out))
        self.assertEqual(car(cdr(out)), cons(y, nil))

    def test_unbox_macro_with_hygiene_strips_syntax_in_first_car(self):
        """
        Regression: the spine walk must not pre-register the whole pair in
        ``seen`` before the first cell, or the first ``car`` is skipped and
        ``Syntax`` boxes remain (e.g. macro output like ``(a b . #*args)``).
        """

        a = symbol('a')
        b = symbol('b')
        form = cons(Syntax(a, None), cons(Syntax(b, None), nil))
        out = unbox_macro_with_hygiene(form, lambda _h: None)
        self.assertFalse(_tree_contains_syntax(out))
        self.assertEqual(out, cons(a, cons(b, nil)))

    def test_unbox_macro_with_hygiene_atom_syntax(self):
        out = unbox_macro_with_hygiene(Syntax(symbol('z'), None), lambda _h: None)
        self.assertEqual(out, symbol('z'))
        self.assertFalse(_tree_contains_syntax(out))

    def test_unbox_macro_strips_syntax(self):
        y = symbol('y')
        boxed = Syntax(cons(y, nil), (1, 1))
        form = cons(symbol('list'), boxed, nil)
        out = unbox_macro(form)
        self.assertFalse(_tree_contains_syntax(out))
        self.assertEqual(car(cdr(out)), cons(y, nil))

    def test_unbox_macro_strips_syntax_in_first_car(self):
        a = symbol('a')
        b = symbol('b')
        form = cons(Syntax(a, None), cons(Syntax(b, None), nil))
        out = unbox_macro(form)
        self.assertFalse(_tree_contains_syntax(out))
        self.assertEqual(out, cons(a, cons(b, nil)))

    def test_unbox_macro_atom_syntax(self):
        out = unbox_macro(Syntax(symbol('z'), None))
        self.assertEqual(out, symbol('z'))
        self.assertFalse(_tree_contains_syntax(out))


# The end.
