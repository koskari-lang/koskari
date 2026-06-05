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
tests.extras.sphinx.test_builtins_partition

Partition and omit rules for koskari.builtins API documentation
"""

import unittest

import pytest

from koskari.extras.sphinx.partition import (
    _classify_member,
    collect_builtins_members,
    member_display_name,
    partition_builtins,
    should_omit_member,
    should_skip_type_documentation,
)
from koskari.lib import FormalsSpec, nil, pair
from koskari.reader.layouts import layout_if
from koskari.syntax import special
from koskari.types import TypePredicate
from koskari.matcher import CALLEDBY, MATCH, VALUE
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_extras


def _make_syntax():
    pat = MATCH(CALLEDBY(), VALUE('x'))

    @special('compile/partition-test', patterns=pat, unpack='match')
    def handler(compiler, caps, tc):
        """
        Partition test syntax
        """

        return None

    return handler


class TestBuiltinsPartition(unittest.TestCase):

    def test_should_omit_constant_names(self):
        self.assertTrue(should_omit_member('True', True))
        self.assertTrue(should_omit_member('False', False))
        self.assertTrue(should_omit_member('None', None))
        self.assertTrue(should_omit_member('nil', nil))


    def test_should_not_omit_pair_type(self):
        self.assertFalse(should_omit_member('pair', pair))


    def test_should_skip_constant_carrier_types(self):
        self.assertTrue(should_skip_type_documentation(bool))
        self.assertTrue(should_skip_type_documentation(type(None)))
        self.assertTrue(should_skip_type_documentation(type(nil)))


    def test_classify_syntax_compile_reader_types(self):
        syn = _make_syntax()
        self.assertEqual(_classify_member('handler', syn), 'compile')
        self.assertEqual(_classify_member('layout_if', layout_if), 'reader')
        self.assertEqual(_classify_member('pair', pair), 'runtime_types')
        self.assertEqual(
            _classify_member('ValueError', ValueError),
            'runtime_exceptions')


    def test_classify_predicate_and_function(self):
        pred = TypePredicate('pair?', pair)
        self.assertEqual(_classify_member('pair?', pred), 'runtime_callables')

        code, ns = build_expr([
            ['CONST', 1],
            ['RETURN', 1],
        ])
        fn = make_callable(code, ns, FormalsSpec())
        self.assertEqual(_classify_member('fn', fn), 'runtime_callables')


    def test_partition_builtins_has_three_top_sections(self):
        members = [
            ('pair', pair),
            ('True', True),
            ('layout_if', layout_if),
            ('handler', _make_syntax()),
        ]
        sections = partition_builtins(members)
        titles = [title for title, _payload in sections]
        self.assertEqual(
            titles,
            ['Runtime features', 'Compile features', 'Reader features'])


    def test_collect_builtins_members_excludes_dunder(self):
        import koskari.builtins as builtins

        names = [name for name, _val in collect_builtins_members(builtins)]
        self.assertNotIn('__name__', names)
        self.assertIn('pair', names)


    def test_member_display_name_prefers_syntax_symbol(self):
        syn = _make_syntax()
        self.assertEqual(
            member_display_name('handler', syn),
            'compile/partition-test')


# The end.
