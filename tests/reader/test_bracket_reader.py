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
The reader builds bracket-pair cells from ``[ ]`` and plain pairs from
``( )``.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import bracket_nil, bracket_pair, kos_repr, nil, pair

from koskari.reader import Reader, source_str

import pytest

pytestmark = pytest.mark.phase_infra


class TestBracketReader(unittest.TestCase):
    """[ ] reads into bracket-pair / bracket-nil, ( ) is unchanged"""

    def read(self, src):
        return Reader().read(source_str(src, "<unittest>"))

    def test_bracket_list_is_bracket_pair(self):
        """[1 2 3] reads as a bracket-pair spine"""

        v = self.read("[1 2 3]")
        self.assertIsInstance(v, bracket_pair)
        self.assertEqual(kos_repr(v), "[1 2 3]")

    def test_bracket_list_terminates_with_bracket_nil(self):
        """a bracket list's last cdr is bracket-nil"""

        v = self.read("[1]")
        self.assertIs(v.cdr, bracket_nil)

    def test_empty_brackets_is_bracket_nil(self):
        """[] reads as the bracket-nil singleton"""

        self.assertIs(self.read("[]"), bracket_nil)

    def test_paren_list_unchanged(self):
        """( ) still reads as plain pairs terminated by nil"""

        v = self.read("(1 2 3)")
        self.assertIs(type(v), pair)
        self.assertEqual(kos_repr(v), "(1 2 3)")
        self.assertIs(v.cdr.cdr.cdr, nil)

    def test_empty_parens_is_nil(self):
        """() still reads as nil"""

        self.assertIs(self.read("()"), nil)

    def test_nested_mixed(self):
        """nested [ ] inside ( ) keeps each cell's own delimiter style"""

        # the idiomatic (let [[a 1]] ...) shape
        v = self.read("(let [[a 1] [b 2]] body)")
        self.assertIs(type(v), pair)            # outer is a paren form
        bindings = v.cdr.car                    # the [[a 1] [b 2]] list
        self.assertIsInstance(bindings, bracket_pair)
        self.assertIsInstance(bindings.car, bracket_pair)   # [a 1]
        self.assertEqual(kos_repr(bindings), "[[a 1] [b 2]]")

    def test_bracket_equals_paren_structurally(self):
        """[1 2 3] and (1 2 3) compare equal despite delimiter style"""

        self.assertEqual(self.read("[1 2 3]"), self.read("(1 2 3)"))

    def test_unquote_of_empty_paren(self):
        """,() reads as an unquote of the empty list, not a splice (no crash)"""

        v = self.read("`(a ,() b)")
        self.assertEqual(
            kos_repr(v),
            "(compile/quasiquote (a (compile/unquote ()) b))")

    def test_unquote_of_empty_bracket(self):
        """,[] is likewise an unquote of the empty bracket list"""

        v = self.read("`(a ,[] b)")
        self.assertEqual(
            kos_repr(v),
            "(compile/quasiquote (a (compile/unquote []) b))")


class TestBracketPairsDisabled(unittest.TestCase):
    """bracket_pairs=False makes [ ] read as plain pairs"""

    def read(self, src):
        reader = Reader(bracket_pairs=False)
        return reader.read(source_str(src, "<unittest>"))

    def test_bracket_list_is_plain_pair(self):
        """[1 2 3] reads as a plain pair list, reprs with parens"""

        v = self.read("[1 2 3]")
        self.assertIs(type(v), pair)
        self.assertEqual(kos_repr(v), "(1 2 3)")

    def test_empty_brackets_is_nil(self):
        """[] reads as nil, not bracket-nil"""

        self.assertIs(self.read("[]"), nil)

    def test_paren_still_works(self):
        """( ) is unaffected"""

        self.assertEqual(kos_repr(self.read("(1 2 3)")), "(1 2 3)")


# The end.
