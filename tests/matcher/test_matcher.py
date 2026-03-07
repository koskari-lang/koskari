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
Tests for koskari.matcher pattern matching.
"""


import unittest

from koskari.lib import cons, keyword, nil, symbol
from koskari.types import unpack
from koskari.matcher import (
    ANY_ORDER,
    BIND,
    DEFINE,
    EXPR,
    LITERAL,
    LITERAL_KW,
    LITERAL_SYM,
    MATCH,
    OPT,
    PAIR,
    REST,
    SYMBOL,
    VALUE,
    match,
)


class TestMatchLiteral(unittest.TestCase):

    def test_literal_matches(self):
        src = cons(keyword('before:'), nil)
        result = match(src, MATCH(LITERAL_KW('before:')))
        self.assertIsNotNone(result)
        self.assertEqual(result, {})

    def test_literal_fails(self):
        src = cons(keyword('body:'), nil)
        result = match(src, MATCH(LITERAL_KW('before:')))
        self.assertIsNone(result)

    def test_literal_sym(self):
        src = cons(symbol('foo'), nil)
        result = match(src, MATCH(LITERAL_SYM('foo')))
        self.assertIsNotNone(result)


class TestMatchBind(unittest.TestCase):

    def test_bind_captures_element(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(BIND('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, x)

    def test_bind_empty_fails(self):
        result = match(nil, MATCH(BIND('val')))
        self.assertIsNone(result)


class TestMatchRest(unittest.TestCase):

    def test_rest_captures_remainder(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, cons(b, nil))
        result = match(src, MATCH(REST('tail')))
        self.assertIsNotNone(result)
        self.assertEqual(list(unpack(result['tail'].source)), [a, b])

    def test_rest_with_bind_before(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, cons(b, nil))
        result = match(src, MATCH(BIND('first'), REST('tail')))
        self.assertIsNotNone(result)
        self.assertEqual(result['first'].source, a)
        self.assertEqual(list(unpack(result['tail'].source)), [b])

    def test_rest_empty_fails(self):
        result = match(nil, MATCH(REST('tail')))
        self.assertIsNone(result)


class TestMatchPairDestructuring(unittest.TestCase):

    def test_match_keyword_bind(self):
        kw = keyword('before:')
        body = cons(symbol('x'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        result = match(src, MATCH(MATCH(LITERAL_KW('before:'), BIND('before_body'))))
        self.assertIsNotNone(result)
        self.assertEqual(result['before_body'].source, body)

    def test_match_wrong_keyword_fails(self):
        kw = keyword('body:')
        body = cons(symbol('x'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        result = match(src, MATCH(MATCH(LITERAL_KW('before:'), BIND('before_body'))))
        self.assertIsNone(result)


class TestMatchPairCapture(unittest.TestCase):

    def test_pair_captures_whole_pair(self):
        p = cons(symbol('a'), symbol('b'))
        src = cons(p, nil)
        result = match(src, MATCH(PAIR('p')))
        self.assertIsNotNone(result)
        self.assertEqual(result['p'].source, p)


class TestMatchOpt(unittest.TestCase):

    def test_opt_matches_when_present(self):
        kw = keyword('after:')
        body = cons(symbol('y'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        result = match(
            src,
            MATCH(OPT(MATCH(LITERAL_KW('after:'), BIND('after_body')))),
        )
        self.assertIsNotNone(result)
        self.assertEqual(result['after_body'].source, body)

    def test_opt_succeeds_when_absent(self):
        result = match(nil, MATCH(OPT(MATCH(LITERAL_KW('after:'), BIND('after_body')))))
        self.assertIsNotNone(result)
        self.assertNotIn('after_body', result)


class TestMatchAnyOrder(unittest.TestCase):

    def test_any_order_matches_clauses(self):
        before = cons(keyword('before:'), cons(symbol('b'), nil))
        body = cons(keyword('body:'), cons(symbol('x'), nil))
        after = cons(keyword('after:'), cons(symbol('a'), nil))
        src = cons(body, cons(before, cons(after, nil)))
        pat = MATCH(ANY_ORDER(
            MATCH(LITERAL_KW('before:'), BIND('before_body')),
            MATCH(LITERAL_KW('body:'), BIND('body')),
            MATCH(LITERAL_KW('after:'), BIND('after_body')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertEqual(result['before_body'].source, cons(symbol('b'), nil))
        self.assertEqual(result['body'].source, cons(symbol('x'), nil))
        self.assertEqual(result['after_body'].source, cons(symbol('a'), nil))

    def test_any_order_body_and_after(self):
        body = cons(keyword('body:'), cons(symbol('x'), nil))
        after = cons(keyword('after:'), cons(symbol('a'), nil))
        src = cons(body, cons(after, nil))
        pat = MATCH(ANY_ORDER(
            MATCH(LITERAL_KW('body:'), BIND('body')),
            MATCH(LITERAL_KW('after:'), BIND('after_body')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertIn('body', result)
        self.assertIn('after_body', result)


class TestMatchDynWindPattern(unittest.TestCase):

    def test_dyn_wind_all_three_keywords(self):
        before = cons(keyword('before:'), cons(symbol('b'), nil))
        body = cons(keyword('body:'), cons(symbol('x'), nil))
        after = cons(keyword('after:'), cons(symbol('a'), nil))
        src = cons(before, cons(body, cons(after, nil)))
        pat = MATCH(ANY_ORDER(
            MATCH(LITERAL_KW('before:'), BIND('before_body')),
            MATCH(LITERAL_KW('body:'), BIND('body')),
            MATCH(LITERAL_KW('after:'), BIND('after_body')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertIn('before_body', result)
        self.assertIn('body', result)
        self.assertIn('after_body', result)


class TestMatchFromTuple(unittest.TestCase):

    def test_tuple_same_as_builder(self):
        kw = keyword('before:')
        body = cons(symbol('x'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        builder = MATCH(MATCH(LITERAL_KW('before:'), BIND('before_body')))
        tup = builder.to_tuple()
        result_builder = match(src, builder)
        result_tup = match(src, tup)
        self.assertIsNotNone(result_tup)
        self.assertEqual(result_builder['before_body'].source,
                         result_tup['before_body'].source)
        self.assertEqual(result_tup['before_body'].source, body)


class TestMatchFailure(unittest.TestCase):

    def test_no_pattern_matches_returns_none(self):
        src = cons(keyword('unknown:'), nil)
        result = match(src, [
            MATCH(LITERAL_KW('before:')),
            MATCH(LITERAL_KW('body:')),
        ])
        self.assertIsNone(result)

    def test_empty_source_bind_fails(self):
        result = match(nil, MATCH(BIND('x')))
        self.assertIsNone(result)


class TestMatchLiterals(unittest.TestCase):

    def test_literals_param_accepted(self):
        sym = symbol('x')
        src = cons(sym, nil)
        result = match(src, MATCH(BIND('val')), literals=frozenset())
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, sym)


class TestMatchExpr(unittest.TestCase):

    def test_expr_captures_element(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(EXPR('expr')))
        self.assertIsNotNone(result)
        self.assertEqual(result['expr'].source, x)


class TestMatchSymbol(unittest.TestCase):

    def test_symbol_captures_symbol(self):
        s = symbol('foo')
        src = cons(s, nil)
        result = match(src, MATCH(SYMBOL('name')))
        self.assertIsNotNone(result)
        self.assertEqual(result['name'].source, s)

    def test_symbol_rejects_non_symbol(self):
        src = cons(keyword('kw'), nil)
        result = match(src, MATCH(SYMBOL('name')))
        self.assertIsNone(result)


class TestMatchDefine(unittest.TestCase):

    def test_define_captures_symbol(self):
        s = symbol('foo')
        src = cons(s, nil)
        result = match(src, MATCH(DEFINE('name')))
        self.assertIsNotNone(result)
        self.assertEqual(result['name'].source, s)

    def test_define_rejects_non_symbol(self):
        src = cons(keyword('kw'), nil)
        result = match(src, MATCH(DEFINE('name')))
        self.assertIsNone(result)

    def test_define_with_value(self):
        name = symbol('x')
        val = symbol('y')
        src = cons(name, cons(val, nil))
        result = match(src, MATCH(DEFINE('name'), VALUE('value')))
        self.assertIsNotNone(result)
        self.assertEqual(result['name'].source, name)
        self.assertEqual(result['value'].source, val)


class TestMatchValue(unittest.TestCase):

    def test_value_captures_element(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(VALUE('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, x)

    def test_value_empty_fails(self):
        result = match(nil, MATCH(VALUE('val')))
        self.assertIsNone(result)

    def test_value_captures_pair(self):
        p = cons(symbol('a'), symbol('b'))
        src = cons(p, nil)
        result = match(src, MATCH(VALUE('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, p)


class TestMatchWithSpecs(unittest.TestCase):

    def test_with_specs_returns_tuple(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(BIND('val')), with_specs=True)
        self.assertIsNotNone(result)
        vals, specs = result
        self.assertIn('val', vals)
        self.assertEqual(vals['val'].source, x)
        self.assertIn('val', specs)
        self.assertIsNotNone(specs['val'])

    def test_with_specs_no_match_returns_none(self):
        result = match(nil, MATCH(BIND('x')), with_specs=True)
        self.assertIsNone(result)


# The end.
