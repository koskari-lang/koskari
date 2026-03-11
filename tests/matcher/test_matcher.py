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
    REPEAT,
    REST,
    SYMBOL,
    VALUE,
    extract_binder_symbols,
    from_tuple,
    match,
)


class TestMatchLiteral(unittest.IsolatedAsyncioTestCase):

    async def test_literal_matches(self):
        src = cons(keyword('before:'), nil)
        result = match(src, MATCH(LITERAL_KW('before:')))
        self.assertIsNotNone(result)
        self.assertEqual(result, {})

    async def test_literal_fails(self):
        src = cons(keyword('body:'), nil)
        result = match(src, MATCH(LITERAL_KW('before:')))
        self.assertIsNone(result)

    async def test_literal_sym(self):
        src = cons(symbol('foo'), nil)
        result = match(src, MATCH(LITERAL_SYM('foo')))
        self.assertIsNotNone(result)


class TestMatchBind(unittest.IsolatedAsyncioTestCase):

    async def test_bind_captures_element(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(BIND('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, x)

    async def test_bind_empty_fails(self):
        result = match(nil, MATCH(BIND('val')))
        self.assertIsNone(result)


class TestMatchRest(unittest.IsolatedAsyncioTestCase):

    async def test_rest_captures_remainder(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, cons(b, nil))
        result = match(src, MATCH(REST('tail')))
        self.assertIsNotNone(result)
        self.assertEqual(list(unpack(result['tail'].source)), [a, b])

    async def test_rest_with_bind_before(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, cons(b, nil))
        result = match(src, MATCH(BIND('first'), REST('tail')))
        self.assertIsNotNone(result)
        self.assertEqual(result['first'].source, a)
        self.assertEqual(list(unpack(result['tail'].source)), [b])

    async def test_rest_empty_fails(self):
        result = match(nil, MATCH(REST('tail')))
        self.assertIsNone(result)


class TestMatchPairDestructuring(unittest.IsolatedAsyncioTestCase):

    async def test_match_keyword_bind(self):
        kw = keyword('before:')
        body = cons(symbol('x'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        result = match(src, MATCH(MATCH(LITERAL_KW('before:'), REST('before_body'))))
        self.assertIsNotNone(result)
        self.assertEqual(result['before_body'].source, cons(symbol('x'), nil))

    async def test_match_wrong_keyword_fails(self):
        kw = keyword('body:')
        body = cons(symbol('x'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        result = match(src, MATCH(MATCH(LITERAL_KW('before:'), REST('before_body'))))
        self.assertIsNone(result)

    async def test_keyword_multi_form_not_unwrapped(self):
        """
        Multi-form keyword clause captures list, not single element
        """
        body = cons(keyword('body:'), cons(symbol('a'), cons(symbol('b'), nil)))
        src = cons(body, nil)
        result = match(src, MATCH(MATCH(LITERAL_KW('body:'), REST('body'))))
        self.assertIsNotNone(result)
        self.assertEqual(
            result['body'].source,
            cons(symbol('a'), cons(symbol('b'), nil)))


class TestMatchPairCapture(unittest.IsolatedAsyncioTestCase):

    async def test_pair_captures_whole_pair(self):
        p = cons(symbol('a'), symbol('b'))
        src = cons(p, nil)
        result = match(src, MATCH(PAIR('p')))
        self.assertIsNotNone(result)
        self.assertEqual(result['p'].source, p)


class TestMatchOpt(unittest.IsolatedAsyncioTestCase):

    async def test_opt_matches_when_present(self):
        kw = keyword('after:')
        body = cons(symbol('y'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        result = match(
            src,
            MATCH(OPT(MATCH(LITERAL_KW('after:'), REST('after_body')))),
        )
        self.assertIsNotNone(result)
        self.assertEqual(result['after_body'].source, cons(symbol('y'), nil))

    async def test_opt_succeeds_when_absent(self):
        result = match(nil, MATCH(OPT(MATCH(LITERAL_KW('after:'), REST('after_body')))))
        self.assertIsNotNone(result)
        self.assertNotIn('after_body', result)


class TestMatchAnyOrder(unittest.IsolatedAsyncioTestCase):

    async def test_any_order_matches_clauses(self):
        before = cons(keyword('before:'), cons(symbol('b'), nil))
        body = cons(keyword('body:'), cons(symbol('x'), nil))
        after = cons(keyword('after:'), cons(symbol('a'), nil))
        src = cons(body, cons(before, cons(after, nil)))
        pat = MATCH(ANY_ORDER(
            MATCH(LITERAL_KW('before:'), REST('before_body')),
            MATCH(LITERAL_KW('body:'), REST('body')),
            MATCH(LITERAL_KW('after:'), REST('after_body')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertEqual(result['before_body'].source, cons(symbol('b'), nil))
        self.assertEqual(result['body'].source, cons(symbol('x'), nil))
        self.assertEqual(result['after_body'].source, cons(symbol('a'), nil))

    async def test_any_order_body_and_after(self):
        body = cons(keyword('body:'), cons(symbol('x'), nil))
        after = cons(keyword('after:'), cons(symbol('a'), nil))
        src = cons(body, cons(after, nil))
        pat = MATCH(ANY_ORDER(
            MATCH(LITERAL_KW('body:'), REST('body')),
            MATCH(LITERAL_KW('after:'), REST('after_body')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertIn('body', result)
        self.assertIn('after_body', result)


class TestMatchDynWindPattern(unittest.IsolatedAsyncioTestCase):

    async def test_dyn_wind_all_three_keywords(self):
        before = cons(keyword('before:'), cons(symbol('b'), nil))
        body = cons(keyword('body:'), cons(symbol('x'), nil))
        after = cons(keyword('after:'), cons(symbol('a'), nil))
        src = cons(before, cons(body, cons(after, nil)))
        pat = MATCH(ANY_ORDER(
            MATCH(LITERAL_KW('before:'), REST('before_body')),
            MATCH(LITERAL_KW('body:'), REST('body')),
            MATCH(LITERAL_KW('after:'), REST('after_body')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertIn('before_body', result)
        self.assertIn('body', result)
        self.assertIn('after_body', result)


class TestMatchFromTuple(unittest.IsolatedAsyncioTestCase):

    async def test_tuple_same_as_builder(self):
        kw = keyword('before:')
        body = cons(symbol('x'), nil)
        elem = cons(kw, body)
        src = cons(elem, nil)
        builder = MATCH(MATCH(LITERAL_KW('before:'), REST('before_body')))
        tup = builder.to_tuple()
        result_builder = match(src, builder)
        result_tup = match(src, tup)
        self.assertIsNotNone(result_tup)
        self.assertEqual(result_builder['before_body'].source,
                         result_tup['before_body'].source)
        self.assertEqual(result_tup['before_body'].source, cons(symbol('x'), nil))


class TestMatchFailure(unittest.IsolatedAsyncioTestCase):

    async def test_no_pattern_matches_returns_none(self):
        src = cons(keyword('unknown:'), nil)
        result = match(src, [
            MATCH(LITERAL_KW('before:')),
            MATCH(LITERAL_KW('body:')),
        ])
        self.assertIsNone(result)

    async def test_empty_source_bind_fails(self):
        result = match(nil, MATCH(BIND('x')))
        self.assertIsNone(result)


class TestMatchLiterals(unittest.IsolatedAsyncioTestCase):

    async def test_literals_param_accepted(self):
        sym = symbol('x')
        src = cons(sym, nil)
        result = match(src, MATCH(BIND('val')), literals=frozenset())
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, sym)


class TestMatchExpr(unittest.IsolatedAsyncioTestCase):

    async def test_expr_captures_element(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(EXPR('expr')))
        self.assertIsNotNone(result)
        self.assertEqual(result['expr'].source, x)


class TestMatchSymbol(unittest.IsolatedAsyncioTestCase):

    async def test_symbol_captures_symbol(self):
        s = symbol('foo')
        src = cons(s, nil)
        result = match(src, MATCH(SYMBOL('name')))
        self.assertIsNotNone(result)
        self.assertEqual(result['name'].source, s)

    async def test_symbol_rejects_non_symbol(self):
        src = cons(keyword('kw'), nil)
        result = match(src, MATCH(SYMBOL('name')))
        self.assertIsNone(result)


class TestMatchDefine(unittest.IsolatedAsyncioTestCase):

    async def test_define_captures_symbol(self):
        s = symbol('foo')
        src = cons(s, nil)
        result = match(src, MATCH(DEFINE('name')))
        self.assertIsNotNone(result)
        self.assertEqual(result['name'].source, s)

    async def test_define_rejects_non_symbol(self):
        src = cons(keyword('kw'), nil)
        result = match(src, MATCH(DEFINE('name')))
        self.assertIsNone(result)

    async def test_define_with_value(self):
        name = symbol('x')
        val = symbol('y')
        src = cons(name, cons(val, nil))
        result = match(src, MATCH(DEFINE('name'), VALUE('value')))
        self.assertIsNotNone(result)
        self.assertEqual(result['name'].source, name)
        self.assertEqual(result['value'].source, val)


class TestMatchValue(unittest.IsolatedAsyncioTestCase):

    async def test_value_captures_element(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(VALUE('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, x)

    async def test_value_empty_fails(self):
        result = match(nil, MATCH(VALUE('val')))
        self.assertIsNone(result)

    async def test_value_captures_pair(self):
        p = cons(symbol('a'), symbol('b'))
        src = cons(p, nil)
        result = match(src, MATCH(VALUE('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, p)


class TestMatchWithSpecs(unittest.IsolatedAsyncioTestCase):

    async def test_with_specs_returns_tuple(self):
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(BIND('val')), with_specs=True)
        self.assertIsNotNone(result)
        vals, specs = result
        self.assertIn('val', vals)
        self.assertEqual(vals['val'].source, x)
        self.assertIn('val', specs)
        self.assertIsNotNone(specs['val'])

    async def test_with_specs_no_match_returns_none(self):
        result = match(nil, MATCH(BIND('x')), with_specs=True)
        self.assertIsNone(result)


class TestMatchRepeat(unittest.IsolatedAsyncioTestCase):

    async def test_repeat_one_or_more_matches(self):
        a = symbol('a')
        b = symbol('b')
        c = symbol('c')
        src = cons(a, cons(b, cons(c, nil)))
        result = match(src, MATCH(REPEAT('items', MATCH(VALUE('x')))))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['items'].matches), 3)
        self.assertEqual(result['items'].matches[0][0]['x'].source, a)
        self.assertEqual(result['items'].matches[1][0]['x'].source, b)
        self.assertEqual(result['items'].matches[2][0]['x'].source, c)

    async def test_repeat_single_match(self):
        x_sym = symbol('x')
        src = cons(x_sym, nil)
        result = match(src, MATCH(REPEAT('items', MATCH(LITERAL_SYM('x')))))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['items'].matches), 1)

    async def test_repeat_fails_when_zero(self):
        y_sym = symbol('y')
        src = cons(y_sym, nil)
        result = match(src, MATCH(REPEAT('items', MATCH(LITERAL_SYM('x')))))
        self.assertIsNone(result)

    async def test_repeat_with_binding(self):
        from koskari.compile.helpers import BINDING

        binding1 = cons(symbol('x'), cons(1, nil))
        binding2 = cons(symbol('y'), cons(2, nil))
        src = cons(binding1, cons(binding2, nil))
        pat = MATCH(REPEAT('bindings', MATCH(BINDING('b'))))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertEqual(len(result['bindings'].matches), 2)
        b1 = result['bindings'].matches[0][0]['b']
        b2 = result['bindings'].matches[1][0]['b']
        self.assertEqual(b1.name, symbol('x'))
        self.assertEqual(b2.name, symbol('y'))
        syms = set(extract_binder_symbols(pat.specs[0], result['bindings']))
        self.assertEqual({str(s) for s in syms}, {'x', 'y'})

    async def test_repeat_to_tuple_roundtrip(self):
        pat = REPEAT('x', MATCH(BIND('y')))
        restored = from_tuple(pat.to_tuple())
        x = symbol('x')
        src = cons(x, nil)
        result = match(src, MATCH(restored))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['x'].matches), 1)
        self.assertEqual(result['x'].matches[0][0]['y'].source, x)

    async def test_repeat_consumes_until_fail(self):
        one = symbol('1')
        two = symbol('2')
        foo = symbol('foo')
        three = symbol('3')
        src = cons(one, cons(two, cons(foo, cons(three, nil))))
        result = match(src, MATCH(REPEAT('nums', MATCH(VALUE('n')))))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['nums'].matches), 4)
        self.assertEqual(result['nums'].matches[0][0]['n'].source, one)
        self.assertEqual(result['nums'].matches[1][0]['n'].source, two)
        self.assertEqual(result['nums'].matches[2][0]['n'].source, foo)
        self.assertEqual(result['nums'].matches[3][0]['n'].source, three)


class TestDuplicateSlotValidation(unittest.IsolatedAsyncioTestCase):

    async def test_duplicate_slot_same_value_succeeds(self):
        a = symbol('a')
        src = cons(a, cons(a, nil))
        result = match(src, MATCH(BIND('x'), BIND('x')))
        self.assertIsNotNone(result)
        self.assertEqual(result['x'].source, a)

    async def test_duplicate_slot_different_value_fails(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, cons(b, nil))
        result = match(src, MATCH(BIND('x'), BIND('x')))
        self.assertIsNone(result)

    async def test_duplicate_slot_different_matcher_type_raises(self):
        foo = symbol('foo')
        bar = symbol('bar')
        src = cons(foo, cons(bar, nil))
        with self.assertRaises(ValueError) as ctx:
            match(src, MATCH(BIND('x'), SYMBOL('x')))
        self.assertIn('duplicate slot', str(ctx.exception))
        self.assertIn('differing matcher type', str(ctx.exception))

    async def test_any_order_duplicate_slot_same_value_succeeds(self):
        x = symbol('x')
        src = cons(x, cons(x, nil))
        pat = MATCH(ANY_ORDER(
            MATCH(BIND('v')),
            MATCH(BIND('v')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertEqual(result['v'].source, x)

    async def test_any_order_duplicate_slot_different_value_fails(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, cons(b, nil))
        pat = MATCH(ANY_ORDER(
            MATCH(BIND('v')),
            MATCH(BIND('v')),
        ))
        result = match(src, pat)
        self.assertIsNone(result)


# The end.
