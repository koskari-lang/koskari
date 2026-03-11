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
from koskari.exceptions import ParametersError, SpecialError
from koskari.compile.helpers import BINDING, FORMALS, gather_formals
from koskari.compile.helpers import gather_parameters_ng
from koskari.matcher import (
    BindingResult,
    DEFINE,
    MATCH,
    VALUE,
    extract_binder_symbols,
    is_binder_spec,
    is_scope_region_spec,
    match,
)


class TestGatherParametersNg(unittest.IsolatedAsyncioTestCase):

    async def test_positional_only(self):
        args = cons(symbol("a"), cons(symbol("b"), nil))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [(0, symbol("a")), (1, symbol("b"))])
        self.assertEqual(kwds_seq, [])

    async def test_empty_list(self):
        args_seq, kwds_seq = gather_parameters_ng(nil)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [])

    async def test_single_symbol_rest(self):
        rest = symbol("args")
        args_seq, kwds_seq = gather_parameters_ng(rest)
        self.assertEqual(args_seq, [("*", rest)])
        self.assertEqual(kwds_seq, [])

    async def test_keywords(self):
        k1 = keyword("k1")
        k2 = keyword("k2")
        v1 = symbol("v1")
        v2 = symbol("v2")
        args = cons(k1, cons(v1, cons(k2, cons(v2, nil))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [("k1", v1), ("k2", v2)])

    async def test_positional_then_keywords(self):
        a = symbol("a")
        b = symbol("b")
        k = keyword("x")
        v = symbol("v")
        args = cons(a, cons(b, cons(k, cons(v, nil))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [(0, a), (1, b)])
        self.assertEqual(kwds_seq, [("x", v)])

    async def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [(0, a), ("*", rest)])
        self.assertEqual(kwds_seq, [])

    async def test_star_starstar(self):
        star_sym = symbol("args")
        starstar_sym = symbol("kwargs")
        k = keyword("*")
        kk = keyword("**")
        args = cons(k, cons(star_sym, cons(kk, cons(starstar_sym, nil))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [("*", star_sym)])
        self.assertEqual(kwds_seq, [("**", starstar_sym)])

    async def test_interleaved_positional_and_star(self):
        a = symbol("a")
        bargs = symbol("bargs")
        c = symbol("c")
        dargs = symbol("dargs")
        k_star = keyword("*")
        args = cons(a, cons(k_star, cons(bargs, cons(c, cons(
            k_star, cons(dargs, nil))))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [
            (0, a), ("*", bargs), (1, c), ("*", dargs)])
        self.assertEqual(kwds_seq, [])

    async def test_interleaved_keyword_and_starstar(self):
        x = keyword("x")
        xval = symbol("xval")
        kw1 = symbol("kw1")
        y = keyword("y")
        yval = symbol("yval")
        kw2 = symbol("kw2")
        kk = keyword("**")
        args = cons(x, cons(xval, cons(kk, cons(kw1, cons(
            y, cons(yval, cons(kk, cons(kw2, nil))))))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [
            ("x", xval), ("**", kw1), ("y", yval), ("**", kw2)])

    async def test_missing_value_for_keyword_raises(self):
        args = cons(keyword("a"), nil)
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters_ng(args)
        self.assertIn("keyword", str(ctx.exception).lower())

    async def test_star_after_keyword_raises(self):
        a = symbol("a")
        k = keyword("a")
        v = symbol("v")
        k_star = keyword("*")
        star_sym = symbol("foo")
        args = cons(a, cons(k, cons(v, cons(k_star, cons(star_sym, nil)))))
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters_ng(args)
        self.assertIn("*", str(ctx.exception))
        self.assertIn("before keyword", str(ctx.exception).lower())

    async def test_positional_after_keyword_raises(self):
        k = keyword("x")
        v = symbol("v")
        extra = symbol("extra")
        args = cons(k, cons(v, cons(extra, nil)))
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters_ng(args)
        self.assertIn("positional", str(ctx.exception).lower())
        self.assertIn("before keyword", str(ctx.exception).lower())

    async def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(ParametersError):
            gather_parameters_ng(42)


class TestGatherFormals(unittest.IsolatedAsyncioTestCase):

    async def test_positional_only(self):
        args = cons(symbol("a"), cons(symbol("b"), nil))
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(len(pos), 2)
        self.assertEqual(str(pos[0]), "a")
        self.assertEqual(str(pos[1]), "b")
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    async def test_single_symbol_rest(self):
        rest = symbol("rest")
        pos, defaults, kwonly, star, starstar = gather_formals(rest)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    async def test_keyword_formals_with_defaults(self):
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

    async def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        pos, defaults, kwonly, star, starstar = gather_formals(args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    async def test_star_then_keyword_only(self):
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

    async def test_starstar(self):
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

    async def test_improper_with_keywords_raises(self):
        a = symbol("a")
        rest = symbol("rest")
        k = keyword("x")
        args = cons(a, cons(rest, cons(k, nil)))
        with self.assertRaises(SpecialError):
            gather_formals(args)

    async def test_non_symbol_positional_raises(self):
        args = cons(keyword("not-a-symbol"), nil)
        with self.assertRaises(SpecialError):
            gather_formals(args)

    async def test_missing_value_for_keyword_raises(self):
        args = cons(symbol("a"), cons(keyword("k"), nil))
        with self.assertRaises(SpecialError):
            gather_formals(args)

    async def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(SpecialError):
            gather_formals(42)


class TestFormalsMatcher(unittest.IsolatedAsyncioTestCase):

    async def test_formals_matcher_positional(self):
        a = symbol('a')
        b = symbol('b')
        formals_list = cons(a, cons(b, nil))
        src = cons(formals_list, nil)
        result = match(src, MATCH(FORMALS('formals')))
        self.assertIsNotNone(result)
        pos, defaults, kwonly, star, starstar = result['formals']
        self.assertEqual(len(pos), 2)
        self.assertEqual(pos[0], a)
        self.assertEqual(pos[1], b)
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    async def test_formals_matcher_symbol_rest(self):
        rest = symbol('rest')
        src = cons(rest, nil)
        result = match(src, MATCH(FORMALS('formals')))
        self.assertIsNotNone(result)
        pos, defaults, kwonly, star, starstar = result['formals']
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)

    async def test_formals_matcher_with_keywords(self):
        a = symbol('a')
        k = keyword('k')
        v = symbol('v')
        formals_list = cons(a, cons(k, cons(v, nil)))
        src = cons(formals_list, nil)
        result = match(src, MATCH(FORMALS('formals')))
        self.assertIsNotNone(result)
        pos, defaults, kwonly, star, starstar = result['formals']
        self.assertEqual(pos, [a])
        self.assertEqual(len(defaults), 1)
        self.assertEqual(defaults[0], (k, v))
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    async def test_formals_matcher_non_formals_fails(self):
        src = cons(42, nil)
        result = match(src, MATCH(FORMALS('formals')))
        self.assertIsNone(result)


class TestBindingMatcher(unittest.IsolatedAsyncioTestCase):

    async def test_binding_matcher_sym_expr(self):
        a = symbol('a')
        one = symbol('1')
        binding_form = cons(a, cons(one, nil))
        src = cons(binding_form, nil)
        result = match(src, MATCH(BINDING('binding')))
        self.assertIsNotNone(result)
        br = result['binding']
        self.assertEqual(br.name, a)
        self.assertEqual(br.value, one)
        self.assertEqual(br.source, binding_form)

    async def test_binding_matcher_expr_as_sym(self):
        one = symbol('1')
        kw = keyword('as')
        a = symbol('a')
        binding_form = cons(one, cons(kw, cons(a, nil)))
        src = cons(binding_form, nil)
        result = match(src, MATCH(BINDING('binding')))
        self.assertIsNotNone(result)
        br = result['binding']
        self.assertEqual(br.name, a)
        self.assertEqual(br.value, one)

    async def test_binding_matcher_allow_unnamed(self):
        one = symbol('1')
        binding_form = cons(one, nil)
        src = cons(binding_form, nil)
        result = match(src, MATCH(BINDING('binding', allow_unnamed=True)))
        self.assertIsNotNone(result)
        br = result['binding']
        self.assertIsNone(br.name)
        self.assertEqual(br.value, one)

    async def test_binding_matcher_non_pair_fails(self):
        src = cons(42, nil)
        result = match(src, MATCH(BINDING('binding')))
        self.assertIsNone(result)

    async def test_is_binder_spec_binding(self):
        m = BINDING('b')
        self.assertTrue(is_binder_spec(m))

    async def test_extract_binder_symbols_binding(self):
        a = symbol('a')
        spec = BINDING('b')
        br = BindingResult(nil, name=a, value=symbol('1'))
        vals = list(extract_binder_symbols(spec, br))
        self.assertEqual(vals, [a])


class TestFormalsMatcherRoleHelpers(unittest.IsolatedAsyncioTestCase):

    async def test_is_binder_spec_formals(self):
        m = FORMALS('f')
        self.assertTrue(is_binder_spec(m))

    async def test_extract_binder_symbols_formals(self):
        a = symbol('a')
        b = symbol('b')
        rest = symbol('rest')
        spec = FORMALS('x')
        pos, defaults, kwonly, star, starstar = ([a, b], [], [], rest, None)
        vals = list(extract_binder_symbols(spec, (pos, defaults, kwonly, star, starstar)))
        self.assertEqual({str(s) for s in vals}, {'a', 'b', 'rest'})

    async def test_is_binder_spec_define(self):
        m = DEFINE('name')
        self.assertTrue(is_binder_spec(m))

    async def test_extract_binder_symbols_define(self):
        x = symbol('x')
        spec = DEFINE('name')
        vals = list(extract_binder_symbols(spec, x))
        self.assertEqual(vals, [x])

    async def test_is_scope_region_spec_value_false(self):
        m = VALUE('val')
        self.assertFalse(is_scope_region_spec(m))


# The end.
