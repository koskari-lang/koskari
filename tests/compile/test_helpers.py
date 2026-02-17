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
Tests for koskari.compile.helpers (gather_parameters, gather_formals).
"""


import unittest

from koskari.types import cons, keyword, nil, symbol
from koskari.compile.exceptions import CompilerException
from koskari.compile.helpers import gather_formals, gather_parameters


class TestGatherParameters(unittest.TestCase):

    def test_positional_only(self):
        args = cons(symbol("a"), cons(symbol("b"), nil))
        pos, kwds, values, star, starstar = gather_parameters(args)
        self.assertEqual(len(pos), 2)
        self.assertEqual(str(pos[0]), "a")
        self.assertEqual(str(pos[1]), "b")
        self.assertEqual(kwds, [])
        self.assertEqual(values, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_empty_list(self):
        pos, kwds, values, star, starstar = gather_parameters(nil)
        self.assertEqual(pos, [])
        self.assertEqual(kwds, [])
        self.assertEqual(values, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_single_symbol_rest(self):
        rest = symbol("args")
        pos, kwds, values, star, starstar = gather_parameters(rest)
        self.assertEqual(pos, [])
        self.assertEqual(kwds, [])
        self.assertEqual(values, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    def test_keywords(self):
        k1 = keyword("k1")
        k2 = keyword("k2")
        v1 = symbol("v1")
        v2 = symbol("v2")
        args = cons(k1, cons(v1, cons(k2, cons(v2, nil))))
        pos, kwds, values, star, starstar = gather_parameters(args)
        self.assertEqual(pos, [])
        self.assertEqual(kwds, [k1, k2])
        self.assertEqual(values, [v1, v2])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_positional_then_keywords(self):
        a = symbol("a")
        b = symbol("b")
        k = keyword("x")
        v = symbol("v")
        args = cons(a, cons(b, cons(k, cons(v, nil))))
        pos, kwds, values, star, starstar = gather_parameters(args)
        self.assertEqual(len(pos), 2)
        self.assertEqual(pos[0], a)
        self.assertEqual(pos[1], b)
        self.assertEqual(kwds, [k])
        self.assertEqual(values, [v])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        pos, kwds, values, star, starstar = gather_parameters(args)
        self.assertEqual(pos, [a])
        self.assertEqual(kwds, [])
        self.assertEqual(values, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    def test_star_starstar(self):
        star_sym = symbol("args")
        starstar_sym = symbol("kwargs")
        k = keyword("*")
        kk = keyword("**")
        args = cons(k, cons(star_sym, cons(kk, cons(starstar_sym, nil))))
        pos, kwds, values, star, starstar = gather_parameters(args)
        self.assertEqual(pos, [])
        self.assertEqual(kwds, [])
        self.assertEqual(values, [])
        self.assertIs(star, star_sym)
        self.assertIs(starstar, starstar_sym)

    def test_missing_value_for_keyword_raises(self):
        args = cons(keyword("a"), nil)
        with self.assertRaises(CompilerException) as ctx:
            gather_parameters(args)
        self.assertIn("keyword", str(ctx.exception).lower())

    def test_leftover_parameters_raises(self):
        a = symbol("a")
        b = symbol("b")
        k = keyword("*")
        star_sym = symbol("s")
        extra = symbol("extra")
        args = cons(k, cons(star_sym, cons(extra, nil)))
        with self.assertRaises(CompilerException):
            gather_parameters(args)

    def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(CompilerException):
            gather_parameters(42)


class TestGatherFormals(unittest.TestCase):

    def test_positional_only(self):
        args = cons(symbol("a"), cons(symbol("b"), nil))
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(len(pos), 2)
        self.assertEqual(str(pos[0]), "a")
        self.assertEqual(str(pos[1]), "b")
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_single_symbol_rest(self):
        rest = symbol("rest")
        pos, defaults, kwonly, star, starstar = gather_formals(rest)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    def test_keyword_formals_with_defaults(self):
        a = symbol("a")
        k = keyword("k")
        v = symbol("v")
        args = cons(a, cons(k, cons(v, nil)))
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(pos, [a])
        self.assertEqual(len(defaults), 1)
        self.assertEqual(defaults[0], (k, v))
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    def test_star_then_keyword_only(self):
        a = symbol("a")
        star_sym = symbol("args")
        k = keyword("k")
        v = symbol("v")
        k_star = keyword("*")
        kk = keyword("**")
        args = cons(a, cons(k_star, cons(star_sym, cons(k, cons(v, nil)))))
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [])
        self.assertIs(star, star_sym)
        self.assertIsNone(starstar)
        self.assertEqual(len(kwonly), 1)
        self.assertEqual(kwonly[0], (k, v))

    def test_starstar(self):
        star_sym = symbol("args")
        starstar_sym = symbol("kwargs")
        k_star = keyword("*")
        kk = keyword("**")
        args = cons(k_star, cons(star_sym, cons(kk, cons(starstar_sym, nil))))
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, star_sym)
        self.assertIs(starstar, starstar_sym)

    def test_improper_with_keywords_raises(self):
        a = symbol("a")
        rest = symbol("rest")
        k = keyword("x")
        args = cons(a, cons(rest, cons(k, nil)))
        with self.assertRaises(CompilerException):
            gather_formals(args)

    def test_non_symbol_positional_raises(self):
        args = cons(keyword("not-a-symbol"), nil)
        with self.assertRaises(CompilerException):
            gather_formals(args)

    def test_missing_value_for_keyword_raises(self):
        args = cons(symbol("a"), cons(keyword("k"), nil))
        with self.assertRaises(CompilerException):
            gather_formals(args)

    def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(CompilerException):
            gather_formals(42)


# The end.
