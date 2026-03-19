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
    LIST,
    MATCH,
    PAIR,
    REPEAT,
    REST,
    SYMBOL,
    VALUE,
    extract_binder_symbols,
    from_tuple,
    is_binder_spec,
    is_scope_region_spec,
    match,
    match1,
)

import pytest

pytestmark = pytest.mark.phase_infra


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
        src = cons(a, b, nil)
        result = match(src, MATCH(REST('tail')))
        self.assertIsNotNone(result)
        self.assertEqual(list(unpack(result['tail'].source)), [a, b])


    async def test_rest_with_bind_before(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, b, nil)
        result = match(src, MATCH(BIND('first'), REST('tail')))
        self.assertIsNotNone(result)
        self.assertEqual(result['first'].source, a)
        self.assertEqual(list(unpack(result['tail'].source)), [b])


    async def test_rest_empty_fails(self):
        result = match(nil, MATCH(REST('tail')))
        self.assertIsNone(result)


class TestMatchPairCapture(unittest.IsolatedAsyncioTestCase):

    async def test_pair_captures_whole_pair(self):
        p = cons(symbol('a'), symbol('b'))
        src = cons(p, nil)
        result = match(src, MATCH(PAIR('p')))
        self.assertIsNotNone(result)
        self.assertEqual(result['p'].source, p)


class TestMatchList(unittest.IsolatedAsyncioTestCase):

    async def test_list_matches_nested_list(self):
        a = symbol('a')
        b = symbol('b')
        inner = cons(a, cons(b, nil))
        src = cons(inner, nil)
        result = match(src, MATCH(LIST(BIND('x'), BIND('y'))))
        self.assertIsNotNone(result)
        self.assertEqual(result['x'].source, a)
        self.assertEqual(result['y'].source, b)


    async def test_list_rejects_non_pair_head(self):
        a = symbol('a')
        src = cons(a, nil)
        result = match(src, MATCH(LIST(BIND('x'))))
        self.assertIsNone(result)


    async def test_list_requires_full_consumption(self):
        a = symbol('a')
        b = symbol('b')
        inner = cons(a, cons(b, nil))
        src = cons(inner, nil)
        result = match(src, MATCH(LIST(BIND('x'))))
        self.assertIsNone(result)


class TestMatchAnyOrder(unittest.IsolatedAsyncioTestCase):

    async def test_any_order_matches_multiple_binds(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, b, nil)
        pat = MATCH(ANY_ORDER(
            MATCH(BIND('x')),
            MATCH(BIND('y')),
        ))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertIn('x', result)
        self.assertIn('y', result)


class TestMatch1(unittest.IsolatedAsyncioTestCase):

    async def test_match1_wraps_value_and_defers_to_match(self):
        x = symbol('x')
        result = match1(x, MATCH(BIND('val')))
        self.assertIsNotNone(result)
        self.assertEqual(result['val'].source, x)


    async def test_match1_no_match_returns_none(self):
        result = match1(symbol('x'), MATCH(LITERAL_KW('before:')))
        self.assertIsNone(result)


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
        src = cons(name, val, nil)
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
        src = cons(a, b, c, nil)
        result = match(src, MATCH(REPEAT('items', VALUE('x'))))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['items'].matches), 3)
        self.assertEqual(result['items'].matches[0][0]['x'].source, a)
        self.assertEqual(result['items'].matches[1][0]['x'].source, b)
        self.assertEqual(result['items'].matches[2][0]['x'].source, c)


    async def test_repeat_single_match(self):
        x_sym = symbol('x')
        src = cons(x_sym, nil)
        result = match(src, MATCH(REPEAT('items', LITERAL_SYM('x'))))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['items'].matches), 1)


    async def test_repeat_fails_when_zero(self):
        y_sym = symbol('y')
        src = cons(y_sym, nil)
        result = match(src, MATCH(REPEAT('items', LITERAL_SYM('x'))))
        self.assertIsNone(result)


    async def test_repeat_to_tuple_roundtrip(self):
        pat = REPEAT('x', BIND('y'))
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
        src = cons(one, two, foo, three, nil)
        result = match(src, MATCH(REPEAT('nums', VALUE('n'))))
        self.assertIsNotNone(result)
        self.assertEqual(len(result['nums'].matches), 4)
        self.assertEqual(result['nums'].matches[0][0]['n'].source, one)
        self.assertEqual(result['nums'].matches[1][0]['n'].source, two)
        self.assertEqual(result['nums'].matches[2][0]['n'].source, foo)
        self.assertEqual(result['nums'].matches[3][0]['n'].source, three)


class TestDuplicateSlotValidation(unittest.IsolatedAsyncioTestCase):

    async def test_duplicate_slot_same_value_succeeds(self):
        a = symbol('a')
        src = cons(a, a, nil)
        result = match(src, MATCH(BIND('x'), BIND('x')))
        self.assertIsNotNone(result)
        self.assertEqual(result['x'].source, a)


    async def test_duplicate_slot_different_value_fails(self):
        a = symbol('a')
        b = symbol('b')
        src = cons(a, b, nil)
        result = match(src, MATCH(BIND('x'), BIND('x')))
        self.assertIsNone(result)


    async def test_duplicate_slot_different_matcher_type_raises(self):
        foo = symbol('foo')
        bar = symbol('bar')
        src = cons(foo, bar, nil)
        with self.assertRaises(ValueError) as ctx:
            match(src, MATCH(BIND('x'), SYMBOL('x')))
        self.assertIn('duplicate slot', str(ctx.exception))
        self.assertIn('differing matcher type', str(ctx.exception))


    async def test_any_order_duplicate_slot_same_value_succeeds(self):
        x = symbol('x')
        src = cons(x, x, nil)
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
        src = cons(a, b, nil)
        pat = MATCH(ANY_ORDER(
            MATCH(BIND('v')),
            MATCH(BIND('v')),
        ))
        result = match(src, pat)
        self.assertIsNone(result)


class TestBinderSpecInfrastructure(unittest.IsolatedAsyncioTestCase):

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
