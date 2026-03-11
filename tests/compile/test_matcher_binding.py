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
Tests for BINDING matcher from koskari.compile.helpers (matcher + compile integration)
"""


import pytest
import unittest

from koskari.lib import cons, keyword, nil, symbol
from koskari.compile.helpers import BINDING, pycons
from koskari.matcher import MATCH, REPEAT, extract_binder_symbols, match, match1


pytestmark = pytest.mark.phase_compile


class TestMatchBinding(unittest.IsolatedAsyncioTestCase):

    async def test_binding_allow_unnamed(self):
        """
        [(compile/call make_mgr)]
        """

        src = pycons(((symbol('compile/call'), symbol('make_mgr')),))
        expr = pycons((symbol('compile/call'), symbol('make_mgr')))
        pat = BINDING('binding', allow_unnamed=True)
        result = match1(src, pat)

        self.assertIsNotNone(result, 'pattern should match')
        br = result['binding']
        self.assertIsNone(br.name, 'binding name should be None for unnamed')
        self.assertEqual(br.source, src)
        self.assertEqual(br.value, expr)


    async def test_binding_allow_unnamed_named(self):
        """
        [x (compile/call make_mgr)]
        """

        src = pycons(
            (symbol('x'),
             (symbol('compile/call'), symbol('make_mgr')),)
        )
        expr = pycons((symbol('compile/call'), symbol('make_mgr')))
        pat = BINDING('binding', allow_unnamed=True)
        result = match1(src, pat)

        self.assertIsNotNone(result, 'pattern should match')
        br = result['binding']
        self.assertEqual(br.name, symbol('x'))
        self.assertEqual(br.source, src)
        self.assertEqual(br.value, expr)


    async def test_binding_allow_unnamed_named_as(self):
        """
        [(compile/call make_mgr) as: x]
        """

        src = pycons(
            ((symbol('compile/call'), symbol('make_mgr')),
             keyword('as:'), symbol('x'))
        )
        expr = pycons((symbol('compile/call'), symbol('make_mgr')))
        pat = BINDING('binding', allow_unnamed=True)
        result = match1(src, pat)

        self.assertIsNotNone(result, 'pattern should match')
        br = result['binding']
        self.assertEqual(br.name, symbol('x'))
        self.assertEqual(br.source, src)
        self.assertEqual(br.value, expr)


class TestRepeatWithBinding(unittest.IsolatedAsyncioTestCase):

    async def test_repeat_with_binding(self):
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
