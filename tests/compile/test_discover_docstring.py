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
Tests for koskari.compile.helpers.discover_docstring.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor.
"""


import unittest

from koskari.compile.helpers import discover_docstring, strip_leading_docstring
from koskari.lib import cons, nil, symbol

import pytest

pytestmark = pytest.mark.phase_compile


class TestDiscoverDocstring(unittest.TestCase):

    def test_nil_body(self):
        self.assertIsNone(discover_docstring(nil))

    def test_leading_str(self):
        body = cons('alpha', cons(symbol('x'), nil))
        self.assertEqual(discover_docstring(body), 'alpha')

    def test_no_leading_str(self):
        body = cons(symbol('x'), nil)
        self.assertIsNone(discover_docstring(body))

    def test_begin_with_str(self):
        begin = symbol('begin')
        inner = cons('beta', cons(symbol('y'), nil))
        head = cons(begin, inner)
        body = cons(head, nil)
        self.assertEqual(discover_docstring(body), 'beta')

    def test_compile_begin_with_str(self):
        cb = symbol('compile/begin')
        inner = cons('gamma', cons(symbol('z'), nil))
        head = cons(cb, inner)
        body = cons(head, nil)
        self.assertEqual(discover_docstring(body), 'gamma')

    def test_begin_without_str_then_more_forms(self):
        begin = symbol('begin')
        inner = cons(symbol('a'), cons(symbol('b'), nil))
        head = cons(begin, inner)
        body = cons(head, cons(symbol('c'), nil))
        self.assertIsNone(discover_docstring(body))

    def test_nested_begin(self):
        begin = symbol('begin')
        inner2 = cons('nested', cons(symbol('u'), nil))
        head2 = cons(begin, inner2)
        inner1 = cons(head2, nil)
        head1 = cons(begin, inner1)
        body = cons(head1, nil)
        self.assertEqual(discover_docstring(body), 'nested')


class TestStripLeadingDocstring(unittest.TestCase):

    def test_nil_body(self):
        doc, rest = strip_leading_docstring(nil)
        self.assertIsNone(doc)
        self.assertIs(rest, nil)

    def test_leading_str_removed(self):
        body = cons('alpha', cons(symbol('x'), nil))
        doc, rest = strip_leading_docstring(body)
        self.assertEqual(doc, 'alpha')
        self.assertEqual(rest, cons(symbol('x'), nil))

    def test_no_leading_str_unchanged(self):
        body = cons(symbol('x'), nil)
        doc, rest = strip_leading_docstring(body)
        self.assertIsNone(doc)
        self.assertIs(rest, body)

    def test_begin_with_str(self):
        begin = symbol('begin')
        inner = cons('beta', cons(symbol('y'), nil))
        head = cons(begin, inner)
        body = cons(head, nil)
        doc, rest = strip_leading_docstring(body)
        self.assertEqual(doc, 'beta')
        self.assertEqual(rest, cons(cons(begin, cons(symbol('y'), nil)), nil))

    def test_nested_begin(self):
        begin = symbol('begin')
        inner2 = cons('nested', cons(symbol('u'), nil))
        head2 = cons(begin, inner2)
        inner1 = cons(head2, nil)
        head1 = cons(begin, inner1)
        body = cons(head1, nil)
        doc, rest = strip_leading_docstring(body)
        self.assertEqual(doc, 'nested')
        self.assertEqual(
            discover_docstring(rest),
            None)


# The end.
