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
Unit tests for the bracket-pair type

bracket-pair is a subclass of pair that records a ``[ ]`` origin. It
shares every pair field and behaves like a pair, but reprs itself with
square brackets. See design/bracket-pair.md.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    bracket_cons, bracket_nil, bracket_pair, cons, kos_repr, nil, nilp, pair)


pytestmark = pytest.mark.phase_lib


class TestBracketPairType(unittest.TestCase):
    """Construction and type identity for bracket-pair"""

    def test_create(self):
        """bracket-pair can be created with a head and a tail"""

        bp = bracket_pair(1, 2)
        self.assertIsInstance(bp, bracket_pair)
        self.assertIs(type(bp), bracket_pair)

    def test_is_a_pair_subclass(self):
        """a bracket-pair is also a pair, so existing pair code accepts it"""

        bp = bracket_pair(1, 2)
        self.assertIsInstance(bp, pair)
        self.assertTrue(issubclass(bracket_pair, pair))

    def test_pair_is_not_a_bracket_pair(self):
        """a plain pair is not a bracket-pair"""

        p = pair(1, 2)
        self.assertNotIsInstance(p, bracket_pair)
        self.assertIsNot(type(p), bracket_pair)

    def test_requires_two_arguments(self):
        """bracket-pair needs both a head and a tail"""

        with self.assertRaises(TypeError):
            bracket_pair(1)

    def test_rejects_keywords(self):
        """bracket-pair takes no named arguments"""

        with self.assertRaises(TypeError):
            bracket_pair(head=1, tail=2)


class TestBracketPairFields(unittest.TestCase):
    """The inherited pair surface works on a bracket-pair"""

    def test_car_cdr(self):
        """car and cdr accessors read the head and tail"""

        bp = bracket_pair(1, 2)
        self.assertEqual(bp.car, 1)
        self.assertEqual(bp.cdr, 2)

    def test_sequence_indexing(self):
        """a bracket-pair indexes like a two-element sequence"""

        bp = bracket_pair("a", "b")
        self.assertEqual(bp[0], "a")
        self.assertEqual(bp[1], "b")

    def test_position_inherited(self):
        """position get/set is inherited from pair"""

        bp = bracket_pair(1, nil)
        self.assertIsNone(bp.position)
        bp.set_position(("file.ki", 3, 7))
        self.assertEqual(bp.get_position(), ("file.ki", 3, 7))
        self.assertEqual(bp.position, ("file.ki", 3, 7))


class TestBracketPairKosRepr(unittest.TestCase):
    """kos_repr renders a bracket-pair spine with square brackets"""

    def test_single_element(self):
        """a one-element bracket list reprs with brackets"""

        bp = bracket_pair(3, nil)
        self.assertEqual(kos_repr(bp), "[3]")

    def test_multiple_elements(self):
        """a multi-element bracket list is space separated in brackets"""

        bp = bracket_pair(1, bracket_pair(2, bracket_pair(3, nil)))
        self.assertEqual(kos_repr(bp), "[1 2 3]")

    def test_string_element_quoted(self):
        """string elements are double-quoted, as in pair kos_repr"""

        bp = bracket_pair("a", nil)
        self.assertEqual(kos_repr(bp), '["a"]')

    def test_improper_tail(self):
        """an improper bracket list reprs with a dotted tail"""

        bp = bracket_pair(1, 2)
        self.assertEqual(kos_repr(bp), "[1 . 2]")

    def test_nil_renders_as_paren_form(self):
        """the canonical empty list renders as its own form, ()"""

        self.assertEqual(kos_repr(nil), "()")

    def test_leading_cell_governs_bracket_then_pair(self):
        """a bracket head cell renders the whole spine in brackets"""

        # a bracket-pair whose tail is a plain pair list
        bp = bracket_pair(0, cons(1, 2, nil))
        self.assertEqual(kos_repr(bp), "[0 1 2]")

    def test_leading_cell_governs_pair_then_bracket(self):
        """a paren head cell renders the whole spine in parens"""

        # a plain pair whose tail is a bracket list
        p = cons(0, bracket_pair(1, nil))
        self.assertEqual(kos_repr(p), "(0 1)")


class TestBracketPairStrRepr(unittest.TestCase):
    """str() and repr() also reflect the bracket origin"""

    def test_str_uses_brackets(self):
        """str() of a bracket list uses square brackets"""

        bp = bracket_pair(1, bracket_pair(2, nil))
        self.assertEqual(str(bp), "[1 2]")

    def test_repr_uses_bracket_cons(self):
        """repr() of a bracket list uses the bracket-cons constructor form"""

        bp = bracket_pair(1, bracket_pair(2, nil))
        self.assertEqual(repr(bp), "bracket-cons(1, 2, nil)")


class TestBracketPairTraversal(unittest.TestCase):
    """Spine-walking operations traverse bracket-pair cells correctly"""

    def _blist(self):
        # the bracket list [1 2 3]
        return bracket_pair(1, bracket_pair(2, bracket_pair(3, nil)))

    def test_length(self):
        """length() counts the whole bracket spine, not just the head"""

        self.assertEqual(self._blist().length(), 3)

    def test_unpack(self):
        """unpack() yields every element of a bracket list"""

        self.assertEqual(list(self._blist().unpack()), [1, 2, 3])

    def test_follow(self):
        """follow() over a bracket list matches the equivalent paren list"""

        self.assertEqual(list(self._blist().follow()),
                         list(cons(1, 2, 3, nil).follow()))

    def test_is_proper(self):
        """a nil-terminated bracket list is proper"""

        self.assertTrue(self._blist().is_proper())

    def test_copy_preserves_bracket_type(self):
        """__copy__ walks the full spine and keeps it bracket"""

        dup = self._blist().__copy__()
        self.assertEqual(list(dup.unpack()), [1, 2, 3])
        self.assertIs(type(dup), bracket_pair)
        self.assertIs(type(dup.cdr), bracket_pair)
        self.assertEqual(kos_repr(dup), "[1 2 3]")

    def test_copy_preserves_per_cell_style(self):
        """__copy__ preserves each cell's style across a mixed spine"""

        # a paren head cell whose tail is a bracket list
        mixed = cons(0, bracket_pair(1, bracket_pair(2, nil)))
        dup = mixed.__copy__()
        self.assertEqual(list(dup.unpack()), [0, 1, 2])
        self.assertIs(type(dup), pair)              # paren head preserved
        self.assertIs(type(dup.cdr), bracket_pair)  # bracket tail preserved

    def test_kos_repr_full_spine(self):
        """kos_repr renders the whole bracket spine"""

        self.assertEqual(kos_repr(self._blist()), "[1 2 3]")

    def test_equal_to_paren_list(self):
        """bracket and paren lists of the same elements compare equal"""

        self.assertEqual(bracket_pair(1, bracket_pair(2, nil)),
                         cons(1, 2, nil))

    def test_equal_two_bracket_lists(self):
        """two structurally equal bracket lists compare equal (no recursion)"""

        self.assertEqual(bracket_pair(1, bracket_pair(2, nil)),
                         bracket_pair(1, bracket_pair(2, nil)))

    def test_equal_across_terminator_flavor(self):
        """a bracket-nil terminator equals a nil terminator at list end"""

        self.assertEqual(bracket_pair(1, bracket_nil), cons(1, nil))


class TestSharedFreelist(unittest.TestCase):
    """pair and bracket-pair share one free list; reuse stamps the type"""

    def test_bracket_block_reused_as_pair(self):
        """a freed bracket-pair block is recycled into a plain pair"""

        bp = bracket_pair(1, 2)
        addr = id(bp)
        del bp                       # -> head of the shared free list
        p = pair(3, 4)               # pops that block, stamps KosPairType
        self.assertEqual(id(p), addr)        # same memory (shared free list)
        self.assertIs(type(p), pair)         # stamped as a plain pair
        self.assertNotIsInstance(p, bracket_pair)

    def test_pair_block_reused_as_bracket(self):
        """a freed pair block is recycled into a bracket-pair"""

        p = pair(1, 2)
        addr = id(p)
        del p
        bp = bracket_pair(3, 4)
        self.assertEqual(id(bp), addr)
        self.assertIs(type(bp), bracket_pair)


class TestBracketCons(unittest.TestCase):
    """bracket-cons: the variadic builder of bracket-pair lists"""

    def test_empty_is_bracket_nil(self):
        """bracket-cons with no args is the empty bracket list"""

        self.assertIs(bracket_cons(), bracket_nil)

    def test_proper_list(self):
        """the last arg is the tail, so a nil tail yields a proper list"""

        bl = bracket_cons(1, 2, 3, nil)
        self.assertIsInstance(bl, bracket_pair)
        self.assertEqual(kos_repr(bl), "[1 2 3]")
        self.assertTrue(bl.is_proper())
        self.assertEqual(list(bl.unpack()), [1, 2, 3])

    def test_all_cells_are_bracket_pairs(self):
        """every spine cell built by bracket-cons is a bracket-pair"""

        bl = bracket_cons(1, 2, 3, nil)
        self.assertIs(type(bl), bracket_pair)
        self.assertIs(type(bl.cdr), bracket_pair)
        self.assertIs(type(bl.cdr.cdr), bracket_pair)
        self.assertIs(bl.cdr.cdr.cdr, nil)

    def test_single_arg_terminates_with_bracket_nil(self):
        """a one-arg bracket-cons terminates with bracket-nil"""

        bl = bracket_cons(1)
        self.assertIsInstance(bl, bracket_pair)
        self.assertEqual(bl.car, 1)
        self.assertIs(bl.cdr, bracket_nil)
        self.assertEqual(kos_repr(bl), "[1]")

    def test_improper(self):
        """a non-empty, non-nil last arg yields an improper bracket pair"""

        bl = bracket_cons(1, 2)
        self.assertEqual(bl.car, 1)
        self.assertEqual(bl.cdr, 2)
        self.assertEqual(kos_repr(bl), "[1 . 2]")

    def test_bracket_nil_tail(self):
        """an explicit bracket-nil tail also yields a proper list"""

        bl = bracket_cons(1, 2, 3, bracket_nil)
        self.assertTrue(bl.is_proper())
        self.assertEqual(kos_repr(bl), "[1 2 3]")
        self.assertIs(bl.cdr.cdr.cdr, bracket_nil)

    def test_equal_to_paren_cons(self):
        """a bracket-cons list equals the cons list of the same elements"""

        self.assertEqual(bracket_cons(1, 2, 3, nil), cons(1, 2, 3, nil))


class TestBracketNil(unittest.TestCase):
    """bracket-nil: the parallel empty-list singleton for bracket style"""

    def test_singleton(self):
        """bracket-nil is a singleton, distinct from nil"""

        self.assertIs(type(bracket_nil)(), bracket_nil)
        self.assertIsNot(bracket_nil, nil)

    def test_is_a_bracket_pair_and_pair(self):
        """bracket-nil is-a bracket-pair (and thus a pair)"""

        self.assertIsInstance(bracket_nil, bracket_pair)
        self.assertIsInstance(bracket_nil, pair)

    def test_not_a_nil_type(self):
        """bracket-nil does not inherit from nil's type — parallel siblings"""

        self.assertNotIsInstance(bracket_nil, type(nil))
        self.assertNotIsInstance(nil, type(bracket_nil))

    def test_falsy_and_empty(self):
        """bracket-nil is falsy and iterates as empty, like nil"""

        self.assertFalse(bracket_nil)
        self.assertEqual(list(bracket_nil), [])

    def test_renders_as_bracket_form(self):
        """bracket-nil renders as its own empty form, []"""

        self.assertEqual(kos_repr(bracket_nil), "[]")
        self.assertEqual(str(bracket_nil), "[]")

    def test_repr_is_named(self):
        """the Python debug repr names the terminator"""

        self.assertEqual(repr(bracket_nil), "bracket-nil")


class TestNilp(unittest.TestCase):
    """nilp / nil? recognizes either empty-list singleton"""

    def test_nilp_both_singletons(self):
        """nilp is true for nil and bracket-nil, false otherwise"""

        self.assertTrue(nilp(nil))
        self.assertTrue(nilp(bracket_nil))
        self.assertFalse(nilp(pair(1, 2)))
        self.assertFalse(nilp(0))
        self.assertFalse(nilp(None))

    def test_empties_compare_equal(self):
        """the two empties compare equal (bracket vs paren is style)"""

        self.assertEqual(nil, bracket_nil)


# The end.
