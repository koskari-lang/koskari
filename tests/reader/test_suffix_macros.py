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
Suffix reader macros: the registry and driver that attach postfix syntax
to an already-read value, and the pragma-gated features built on it.

A suffix macro dispatches on the character following a completed value,
consumes exactly one suffix, and returns a wrapped value; the driver
re-enters, so chains compose without any one macro knowing about the
others.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import kos_repr

from koskari.reader import (DEFER, READ_EOF, Reader, ReaderSyntaxError,
                            source_str)

import pytest

pytestmark = pytest.mark.phase_infra


def _consume_one(stream, value, position):
    """
    A well-behaved suffix macro: consumes its trigger, wraps the value
    """

    stream.read(1)
    return f"one({value})"


def _decline(stream, value, position):
    """
    A suffix macro that always declines without consuming
    """

    return DEFER


def _claim_without_consuming(stream, value, position):
    """
    A misbehaving suffix macro: claims the suffix but advances nothing
    """

    return value


class SuffixReaderMixin:
    """
    Read helpers over a bare (non-layout) reader
    """

    def make_reader(self):
        return Reader()

    def read(self, src, reader=None):
        reader = reader if reader is not None else self.make_reader()
        return reader.read(source_str(src, "<unittest>"))

    def read_all(self, src, reader=None, limit=8):
        reader = reader if reader is not None else self.make_reader()
        stream = source_str(src, "<unittest>")
        out = []
        while len(out) < limit:
            value = reader.read(stream)
            if value is READ_EOF:
                break
            out.append(value)
        return out


class TestSuffixMacroRegistry(SuffixReaderMixin, unittest.TestCase):
    """
    Registration, stacking, and removal of suffix macros
    """

    def test_registered_macro_wraps_the_value(self):
        """
        A macro registered for a character claims it after a value
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)
        self.assertEqual(self.read("(foo)!", reader), "one((foo))")

    def test_applies_after_any_value_kind(self):
        """
        Dispatch is on the following character, not on the value's type
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)
        self.assertEqual(self.read('"x"!', reader), 'one(x)')

    def test_unregistered_character_is_left_alone(self):
        """
        A character with no suffix macro ends the value
        """

        values = self.read_all("(foo)!")
        self.assertEqual([kos_repr(v) for v in values], ["(foo)", "!"])

    def test_get_suffix_macros_is_newest_first(self):
        """
        Each registration prepends to that character's stack
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)
        reader.add_suffix_macro("!", _decline)

        entries = reader.get_suffix_macros("!")
        self.assertEqual([fn for fn, _pred in entries],
                         [_decline, _consume_one])

    def test_get_suffix_macro_returns_the_newest(self):
        """
        The single-entry accessor returns the head of the stack
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)
        reader.add_suffix_macro("!", _decline)

        self.assertEqual(reader.get_suffix_macro("!"), (_decline, None))

    def test_get_suffix_macro_of_unregistered_is_none(self):
        """
        An unregistered character has no newest entry
        """

        self.assertIsNone(self.make_reader().get_suffix_macro("!"))

    def test_clear_suffix_macro_removes_the_stack(self):
        """
        Clearing a character drops every entry for it
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)
        reader.clear_suffix_macro("!")

        self.assertEqual(reader.get_suffix_macros("!"), [])
        self.assertEqual(kos_repr(self.read("(foo)!", reader)), "(foo)")

    def test_temporary_suffix_macro_applies_then_reverts(self):
        """
        The context manager installs for its extent only
        """

        reader = self.make_reader()

        with reader.temporary_suffix_macro("!", _consume_one):
            self.assertEqual(self.read("(foo)!", reader), "one((foo))")

        self.assertEqual(reader.get_suffix_macros("!"), [])


class TestSuffixMacroDispatch(SuffixReaderMixin, unittest.TestCase):
    """
    The driver's contract with the macros it dispatches to
    """

    def test_defer_leaves_value_and_stream_untouched(self):
        """
        A declining macro yields the value and does not consume
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _decline)

        values = self.read_all("(foo)!", reader)
        self.assertEqual([kos_repr(v) for v in values], ["(foo)", "!"])

    def test_defer_falls_through_to_the_older_entry(self):
        """
        DEFER tries the next entry in that character's stack
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)
        reader.add_suffix_macro("!", _decline)

        self.assertEqual(self.read("(foo)!", reader), "one((foo))")

    def test_false_predicate_skips_without_calling(self):
        """
        A predicate returning false is an early DEFER; the macro never runs
        """

        def _explode(stream, value, position):
            raise AssertionError("macro_fn ran despite a false predicate")

        reader = self.make_reader()
        reader.add_suffix_macro(
            "!", _explode, predicate=lambda stream, char, value: False)

        values = self.read_all("(foo)!", reader)
        self.assertEqual([kos_repr(v) for v in values], ["(foo)", "!"])

    def test_predicate_receives_the_attaching_value(self):
        """
        The predicate is given the value the suffix would attach to
        """

        seen = []

        def _record(stream, char, value):
            seen.append((char, kos_repr(value)))
            return True

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one, predicate=_record)
        self.read("(foo)!", reader)

        self.assertEqual(seen, [("!", "(foo)")])

    def test_driver_re_enters_for_chains(self):
        """
        Each pass applies one suffix; the driver loops until none claim
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _consume_one)

        self.assertEqual(self.read("(foo)!!", reader), "one(one((foo)))")

    def test_claiming_without_consuming_is_an_error(self):
        """
        A macro that advances nothing is caught rather than looping forever
        """

        reader = self.make_reader()
        reader.add_suffix_macro("!", _claim_without_consuming)

        with self.assertRaises(ReaderSyntaxError) as raised:
            self.read("(foo)!", reader)

        self.assertIn("consumed no input", str(raised.exception))


class TestIndexBrackets(SuffixReaderMixin, unittest.TestCase):
    """
    The postfix index-bracket suffix, ``reader/index-brackets`` enabled
    """

    def make_reader(self):
        reader = Reader()
        reader.index_brackets = True
        return reader

    def test_attached_bracket_is_an_item_access(self):
        """
        foo[0] reads as (item foo 0)
        """

        self.assertEqual(kos_repr(self.read("foo[0]")), "(item foo 0)")

    def test_chains_left_to_right(self):
        """
        grid[y][x] reads as (item (item grid y) x)
        """

        self.assertEqual(kos_repr(self.read("grid[y][x]")),
                         "(item (item grid y) x)")

    def test_attaches_to_a_form(self):
        """
        The receiver need not be an atom
        """

        self.assertEqual(kos_repr(self.read("(f x)[0]")), "(item (f x) 0)")

    def test_attaches_to_a_string(self):
        """
        Any completed value takes the suffix
        """

        self.assertEqual(kos_repr(self.read('"ab"[0]')), '(item "ab" 0)')

    def test_multiple_keys_in_one_group(self):
        """
        foo[1 2] carries both keys into one item form
        """

        self.assertEqual(kos_repr(self.read("foo[1 2]")), "(item foo 1 2)")

    def test_whitespace_breaks_the_attachment(self):
        """
        foo [a] is two forms, not an item access
        """

        values = self.read_all("foo [a]")
        self.assertEqual([kos_repr(v) for v in values], ["foo", "[a]"])

    def test_empty_group_is_an_error(self):
        """
        An index group must carry a key
        """

        with self.assertRaises(ReaderSyntaxError) as raised:
            self.read("foo[]")

        self.assertIn("require a key", str(raised.exception))


class TestIndexBracketsDisabled(SuffixReaderMixin, unittest.TestCase):
    """
    With ``reader/index-brackets`` off, the predicate declines every time
    """

    def make_reader(self):
        reader = Reader()
        reader.index_brackets = False
        return reader

    def test_attached_bracket_stays_a_separate_form(self):
        """
        foo[0] is two forms when the feature is off
        """

        values = self.read_all("foo[0]")
        self.assertEqual([kos_repr(v) for v in values], ["foo", "[0]"])

    def test_empty_group_is_not_an_error(self):
        """
        The error belongs to the feature, not to the reader
        """

        values = self.read_all("foo[]")
        self.assertEqual([kos_repr(v) for v in values], ["foo", "[]"])


class TestDotAttrs(SuffixReaderMixin, unittest.TestCase):
    """
    Dotted attribute access, ``reader/dot-attrs`` enabled (the baseline)
    """

    def test_dotted_atom_is_an_attr_form(self):
        """
        foo.bar reads as (compile/attr foo bar)
        """

        self.assertEqual(kos_repr(self.read("foo.bar")),
                         "(compile/attr foo bar)")

    def test_dotted_atom_is_left_associative(self):
        """
        foo.bar.baz nests left to right
        """

        self.assertEqual(kos_repr(self.read("foo.bar.baz")),
                         "(compile/attr (compile/attr foo bar) baz)")

    def test_attaches_to_a_form(self):
        """
        The receiver may be a form, which the atom rule alone cannot do
        """

        self.assertEqual(kos_repr(self.read("(get-fn x).baz")),
                         "(compile/attr (get-fn x) baz)")

    def test_attaches_to_a_string_literal(self):
        """
        "".join is a method call once the parens are around it
        """

        self.assertEqual(kos_repr(self.read('"".join')),
                         '(compile/attr "" join)')

    def test_suffix_chains_on_a_form(self):
        """
        (f x).a.b nests left to right through driver re-entry
        """

        self.assertEqual(kos_repr(self.read("(f x).a.b")),
                         "(compile/attr (compile/attr (f x) a) b)")

    def test_attribute_names_admit_dash_and_slash(self):
        """
        The suffix accepts the same name characters the atom rule does
        """

        self.assertEqual(kos_repr(self.read("(f x).bar-baz")),
                         "(compile/attr (f x) bar-baz)")
        self.assertEqual(kos_repr(self.read("(f x).a/b")),
                         "(compile/attr (f x) a/b)")

    def test_whitespace_breaks_the_attachment(self):
        """
        A space leaves .bar an ordinary symbol
        """

        values = self.read_all("foo .bar")
        self.assertEqual([kos_repr(v) for v in values], ["foo", ".bar"])

    def test_leading_dot_call_is_untouched(self):
        """
        The (.name obj) method spelling still reads as itself
        """

        self.assertEqual(kos_repr(self.read("(.baz obj)")), "(.baz obj)")

    def test_float_is_still_a_float(self):
        """
        A numeric atom is claimed before the dotted pattern is reached
        """

        self.assertEqual(self.read("1.5"), 1.5)

    def test_ellipsis_is_still_ellipsis(self):
        """
        ... is claimed by its own atom pattern
        """

        self.assertIs(self.read("..."), ...)

    def test_keyword_is_unaffected(self):
        """
        A trailing colon is a keyword, not an attribute
        """

        self.assertEqual(kos_repr(self.read("foo:")), "foo:")


class TestDotAttrsMalformed(SuffixReaderMixin, unittest.TestCase):
    """
    A dot with no name after it is an error in both spellings
    """

    def assert_read_error(self, src, expected):
        with self.assertRaises(ReaderSyntaxError) as raised:
            self.read(src)
        self.assertIn(expected, str(raised.exception))

    def test_trailing_dot_on_an_atom(self):
        """
        foo. is rejected rather than falling through to a symbol
        """

        self.assert_read_error("foo.", "attribute name missing")

    def test_empty_segment_within_an_atom(self):
        """
        foo..bar names the empty segment
        """

        self.assert_read_error("foo..bar", "attribute name missing")

    def test_non_identifier_segment(self):
        """
        A segment that is not an identifier is named in the error
        """

        self.assert_read_error("foo.9x", "invalid attribute name '9x'")

    def test_trailing_dot_on_a_form(self):
        """
        The suffix path reaches the error by its own route
        """

        self.assert_read_error("(f x).", "attribute name expected")

    def test_dot_before_a_bracket(self):
        """
        foo.[0] has no attribute name
        """

        self.assert_read_error("foo.[0]", "attribute name missing")


class TestDotAttrsWithIndexBrackets(SuffixReaderMixin, unittest.TestCase):
    """
    The two suffixes interleave, which is the driver's doing rather than
    either macro's
    """

    def make_reader(self):
        reader = Reader()
        reader.index_brackets = True
        return reader

    def test_attr_after_index(self):
        """
        foo[0].bar reads the attribute off the item access
        """

        self.assertEqual(kos_repr(self.read("foo[0].bar")),
                         "(compile/attr (item foo 0) bar)")

    def test_index_after_attr(self):
        """
        foo.bar[0] indexes the attribute
        """

        self.assertEqual(kos_repr(self.read("foo.bar[0]")),
                         "(item (compile/attr foo bar) 0)")

    def test_arbitrary_alternation(self):
        """
        The two kinds alternate freely, left to right
        """

        self.assertEqual(
            kos_repr(self.read("(f x)[0].a.b[1]")),
            "(item (compile/attr (compile/attr (item (f x) 0) a) b) 1)")


class TestDotAttrsDisabled(SuffixReaderMixin, unittest.TestCase):
    """
    With ``reader/dot-attrs`` off, one switch silences both spellings
    """

    def make_reader(self):
        reader = Reader()
        reader.dot_attrs = False
        return reader

    def test_dotted_atom_stays_a_symbol(self):
        """
        foo.bar is an ordinary symbol when the feature is off
        """

        self.assertEqual(kos_repr(self.read("foo.bar")), "foo.bar")

    def test_suffix_does_not_attach(self):
        """
        (get-fn x).baz is two forms when the feature is off
        """

        values = self.read_all("(get-fn x).baz")
        self.assertEqual([kos_repr(v) for v in values],
                         ["(get-fn x)", ".baz"])

    def test_trailing_dot_is_not_an_error(self):
        """
        The error belongs to the feature, not to the reader
        """

        self.assertEqual(kos_repr(self.read("foo.")), "foo.")

    def test_float_is_unaffected(self):
        """
        Numeric atoms never depended on this setting
        """

        self.assertEqual(self.read("1.5"), 1.5)


# The end.
