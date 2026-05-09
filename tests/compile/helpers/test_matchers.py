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
Tests for matchers from koskari.compile.helpers (BINDING, BINDINGS, USING,
USING_UNPACK, FORMALS).
"""


import pytest
import unittest

from koskari.lib import cons, keyword, nil, symbol
from koskari.exceptions import SpecialError
from koskari.compile.helpers import (
    BINDING,
    FORMALS,
    USING,
    USING_UNPACK,
    helper_using,
    pycons,
)
from koskari.matcher import (
    BindingResult,
    MATCH,
    REPEAT,
    UsingResult,
    extract_binder_symbols,
    is_binder_spec,
    match,
    match1,
)


pytestmark = pytest.mark.phase_compile


class TestFormalsMatcher(unittest.TestCase):

    def test_formals_matcher_positional(self):
        a = symbol('a')
        b = symbol('b')
        formals_list = cons(a, b, nil)
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

    def test_formals_matcher_symbol_rest(self):
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

    def test_formals_matcher_with_keywords(self):
        a = symbol('a')
        k = keyword('k')
        v = symbol('v')
        formals_list = cons(a, k, v, nil)
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

    def test_formals_matcher_non_formals_fails(self):
        src = cons(42, nil)
        result = match(src, MATCH(FORMALS('formals')))
        self.assertIsNone(result)

    def test_is_binder_spec_formals(self):
        m = FORMALS('f')
        self.assertTrue(is_binder_spec(m))

    def test_extract_binder_symbols_formals(self):
        a = symbol('a')
        b = symbol('b')
        rest = symbol('rest')
        spec = FORMALS('x')
        pos, defaults, kwonly, star, starstar = ([a, b], [], [], rest, None)
        vals = list(extract_binder_symbols(spec, (pos, defaults, kwonly, star, starstar)))
        self.assertEqual({str(s) for s in vals}, {'a', 'b', 'rest'})


class TestBindingMatcher(unittest.TestCase):

    def test_binding_matcher_sym_expr(self):
        a = symbol('a')
        one = symbol('1')
        binding_form = cons(a, one, nil)
        src = cons(binding_form, nil)
        result = match(src, MATCH(BINDING('binding')))
        self.assertIsNotNone(result)
        br = result['binding']
        self.assertEqual(br.name, a)
        self.assertEqual(br.value, one)
        self.assertEqual(br.source, binding_form)

    def test_binding_matcher_expr_as_sym(self):
        one = symbol('1')
        kw = keyword('as')
        a = symbol('a')
        binding_form = cons(one, kw, a, nil)
        src = cons(binding_form, nil)
        result = match(src, MATCH(BINDING('binding')))
        self.assertIsNotNone(result)
        br = result['binding']
        self.assertEqual(br.name, a)
        self.assertEqual(br.value, one)

    def test_binding_matcher_non_pair_fails(self):
        src = cons(42, nil)
        result = match(src, MATCH(BINDING('binding')))
        self.assertIsNone(result)

    def test_is_binder_spec_binding(self):
        m = BINDING('b')
        self.assertTrue(is_binder_spec(m))

    def test_extract_binder_symbols_binding(self):
        a = symbol('a')
        spec = BINDING('b')
        br = BindingResult(nil, name=a, value=symbol('1'))
        vals = list(extract_binder_symbols(spec, br))
        self.assertEqual(vals, [a])


class TestUsingMatcher(unittest.TestCase):

    def test_using_unnamed(self):
        """
        [(compile/call make_mgr)]
        """
        src = pycons(((symbol('compile/call'), symbol('make_mgr')),))
        expr = pycons((symbol('compile/call'), symbol('make_mgr')))
        pat = USING('binding')
        result = match1(src, pat)

        self.assertIsNotNone(result, 'pattern should match')
        br = result['binding']
        self.assertIsInstance(br, UsingResult)
        self.assertIsNone(br.name, 'using name should be None for unnamed')
        self.assertEqual(br.source, src)
        self.assertEqual(br.value, expr)

        name, parsed_expr = helper_using(src)
        self.assertIsNone(name)
        self.assertEqual(parsed_expr, expr)

    def test_using_unnamed_named_as(self):
        """
        [(compile/call make_mgr) as: x]
        """
        src = pycons(
            ((symbol('compile/call'), symbol('make_mgr')),
             keyword('as:'), symbol('x'))
        )
        expr = pycons((symbol('compile/call'), symbol('make_mgr')))
        pat = USING('binding')
        result = match1(src, pat)

        self.assertIsNotNone(result, 'pattern should match')
        br = result['binding']
        self.assertIsInstance(br, UsingResult)
        self.assertEqual(br.name, symbol('x'))
        self.assertEqual(br.source, src)
        self.assertEqual(br.value, expr)

        name, parsed_expr = helper_using(src)
        self.assertEqual(name, symbol('x'))
        self.assertEqual(parsed_expr, expr)

    def test_using_named_expr_as_first(self):
        """
        [x (compile/call make_mgr)]
        """
        src = pycons((symbol('x'), (symbol('compile/call'), symbol('make_mgr'))))
        expr = pycons((symbol('compile/call'), symbol('make_mgr')))
        pat = USING('binding')
        result = match1(src, pat)

        self.assertIsNotNone(result, 'pattern should match')
        br = result['binding']
        self.assertIsInstance(br, UsingResult)
        self.assertEqual(br.name, symbol('x'))
        self.assertEqual(br.source, src)
        self.assertEqual(br.value, expr)

        name, parsed_expr = helper_using(src)
        self.assertEqual(name, symbol('x'))
        self.assertEqual(parsed_expr, expr)

    def test_using_rejects_structured_lhs(self):
        a, b = symbol('a'), symbol('b')
        it = symbol('items')
        lhs = cons(a, b, nil)
        cell = cons(lhs, it, nil)
        pat = USING('binding')
        self.assertIsNone(match1(cell, pat))

        with self.assertRaises(SpecialError):
            helper_using(cell)


class TestUsingUnpackMatcher(unittest.TestCase):

    def test_using_unpack_pair_and_expr(self):
        a, b = symbol('a'), symbol('b')
        it = symbol('items')
        lhs = cons(a, b, nil)
        cell = cons(lhs, it, nil)
        src = cons(cell, nil)
        pat = MATCH(USING_UNPACK('binding'))
        result = match(src, pat)
        self.assertIsNotNone(result)
        br = result['binding']
        self.assertIsInstance(br, UsingResult)
        self.assertIsNone(br.name)
        self.assertIs(br.lhs, lhs)
        self.assertEqual(br.value, it)
        self.assertEqual(br.source, cell)

    def test_extract_binder_symbols_using_unpack(self):
        a, b = symbol('a'), symbol('b')
        lhs = cons(a, b, nil)
        cell = cons(lhs, symbol('x'), nil)
        spec = USING_UNPACK('binding')
        br = UsingResult(cell, name=None, value=symbol('x'), lhs=lhs)
        vals = list(extract_binder_symbols(spec, br))
        self.assertEqual(vals, [a, b])


class TestRepeatWithBinding(unittest.TestCase):

    def test_repeat_with_binding(self):
        binding1 = cons(symbol('x'), 1, nil)
        binding2 = cons(symbol('y'), 2, nil)
        src = cons(binding1, binding2, nil)
        pat = MATCH(REPEAT('bindings', BINDING('b')))
        result = match(src, pat)
        self.assertIsNotNone(result)
        self.assertEqual(len(result['bindings'].matches), 2)
        b1 = result['bindings'].matches[0][0]['b']
        b2 = result['bindings'].matches[1][0]['b']
        self.assertEqual(b1.name, symbol('x'))
        self.assertEqual(b2.name, symbol('y'))
        syms = set(extract_binder_symbols(pat.specs[0], result['bindings']))
        self.assertEqual({str(s) for s in syms}, {'x', 'y'})


# The end.
