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
Tests for the runtime case-pattern dialect: the CASE_ELEMENT_MATCHER /
CASE_DELIM_MATCHER walkers (parse_case_element, parse_case_pattern)
that classify match/case patterns by head symbol or marker, and
end-to-end matching of the classified patterns against runtime values

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from dataclasses import dataclass

from koskari.lib import cons, keyword, nil, symbol
from koskari.matcher import (
    APP,
    CAPTURE,
    CLASS,
    FORM,
    LITERAL,
    MAP,
    MATCH,
    MatchCompiler,
    NOT,
    ONEOF,
    PREDICATE,
    REST,
    parse_case_element,
    parse_case_pattern,
    parse_matcher,
    thaw,
)


pytestmark = pytest.mark.phase_infra


def _list(*items):
    return cons(*items, nil) if items else nil


def _delim(*items):
    """
    Wrap pattern content the way a match clause delimiter delivers it
    """

    return _list(*items)


def _frozen(pattern):
    return MatchCompiler().lower(MATCH(pattern))


def _match_value(pattern, subject, **externs):
    """
    Thaw a classified pattern and run it against one runtime value
    """

    m = thaw(_frozen(pattern))
    r = m.match((subject,), **externs)
    caps = {k: getattr(v, "datum", v) for k, v in r.captures.items()}
    return r.matched, caps


@dataclass
class Point:
    x: int
    y: int


class TestClassifyAtoms(unittest.TestCase):

    def test_symbol_is_capture(self):
        pat = parse_case_pattern(_delim(symbol("x")))
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(_frozen(pat)[1], ("x",))

    def test_int_is_literal(self):
        pat = parse_case_pattern(_delim(5))
        self.assertIsInstance(pat, LITERAL)
        self.assertEqual(_frozen(pat)[1], ())

    def test_string_is_literal(self):
        pat = parse_case_pattern(_delim("hello"))
        self.assertIsInstance(pat, LITERAL)

    def test_quoted_symbol_is_literal_symbol(self):
        pat = parse_case_pattern(
            _delim(_list(symbol("quote"), symbol("foo"))))
        self.assertIsInstance(pat, LITERAL)

    def test_empty_delimiter_raises(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(nil)


class TestClassifyTyped(unittest.TestCase):

    def test_multi_token_typed(self):
        # [[x :: int]]
        pat = parse_case_pattern(
            _delim(symbol("x"), keyword("::"), symbol("int")))
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("x",))
        self.assertEqual(frozen[2], ("int",))

    def test_single_form_typed(self):
        # [[(x :: int)]]
        pat = parse_case_pattern(
            _delim(_list(symbol("x"), keyword("::"), symbol("int"))))
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("x",))
        self.assertEqual(frozen[2], ("int",))

    def test_anonymous_typed(self):
        # [[:: int]]
        pat = parse_case_pattern(
            _delim(keyword("::"), symbol("int")))
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ())
        self.assertEqual(frozen[2], ("int",))


class TestClassifySequence(unittest.TestCase):

    def test_list_head_is_sequence(self):
        # #[a b c]
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), symbol("a"), symbol("b"), symbol("c"))))
        self.assertIsInstance(pat, FORM)
        self.assertEqual(_frozen(pat)[1], ("a", "b", "c"))

    def test_tuple_head_is_sequence(self):
        # #(a b)
        pat = parse_case_pattern(_delim(
            _list(symbol("#tuple"), symbol("a"), symbol("b"))))
        self.assertIsInstance(pat, FORM)
        self.assertEqual(_frozen(pat)[1], ("a", "b"))

    def test_star_rest(self):
        # #[a *: r]
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), symbol("a"), keyword("*"), symbol("r"))))
        self.assertEqual(_frozen(pat)[1], ("a", "r"))

    def test_empty_sequence(self):
        # #[]
        pat = parse_case_pattern(_delim(_list(symbol("#list"))))
        self.assertIsInstance(pat, FORM)
        self.assertEqual(_frozen(pat)[1], ())

    def test_nested_sequence(self):
        # #[a #[b c]]
        inner = _list(symbol("#list"), symbol("b"), symbol("c"))
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), symbol("a"), inner)))
        self.assertEqual(_frozen(pat)[1], ("a", "b", "c"))

    def test_sequence_element_literal(self):
        # #[1 x]
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), 1, symbol("x"))))
        self.assertEqual(_frozen(pat)[1], ("x",))

    def test_bare_list_is_never_a_sequence(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(
                _delim(symbol("a"), symbol("b"), symbol("c")))


class TestClassifyClass(unittest.TestCase):

    def test_positional_fields(self):
        # Point[a b] -> (item Point a b)
        pat = parse_case_pattern(_delim(
            _list(symbol("item"), symbol("Point"),
                  symbol("a"), symbol("b"))))
        self.assertIsInstance(pat, CLASS)
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("a", "b"))
        self.assertEqual(frozen[2], ("Point",))

    def test_keyword_field_literal(self):
        # Point[x: 0 y] -> attr x must equal 0, self-bind attr y
        pat = parse_case_pattern(_delim(
            _list(symbol("item"), symbol("Point"),
                  keyword("x"), 0, symbol("y"))))
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("y",))
        self.assertEqual(frozen[2], ("Point",))

    def test_no_fields(self):
        # Name[] -> bare isinstance
        pat = parse_case_pattern(_delim(
            _list(symbol("item"), symbol("Point"))))
        self.assertIsInstance(pat, CLASS)
        self.assertEqual(_frozen(pat)[2], ("Point",))

    def test_keyword_field_missing_pattern_raises(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(_delim(
                _list(symbol("item"), symbol("Point"), keyword("x"))))


class TestClassifyCombinators(unittest.TestCase):

    def test_or_is_oneof(self):
        pat = parse_case_pattern(_delim(_list(symbol("or"), 1, 2)))
        self.assertIsInstance(pat, ONEOF)

    def test_empty_or_raises(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(_delim(_list(symbol("or"))))

    def test_not_is_consuming(self):
        # the case dialect's (not p) is a whole-element pattern: the
        # engine's zero-width NOT paired with a consuming any, so it can
        # match (and reject) a value on its own
        pat = parse_case_pattern(_delim(_list(symbol("not"), 5)))
        self.assertIsInstance(pat, MATCH)
        self.assertTrue(_match_value(pat, 6)[0])
        self.assertFalse(_match_value(pat, 5)[0])

    def test_not_in_sequence(self):
        # #[(not 9) b] -- element-position not consumes its element
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"),
                  _list(symbol("not"), 9), symbol("b"))))
        matched, caps = _match_value(pat, (1, 2))
        self.assertTrue(matched)
        self.assertEqual(caps["b"], 2)
        self.assertFalse(_match_value(pat, (9, 2))[0])

    def test_predicate(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("?"), symbol("even"))))
        self.assertIsInstance(pat, PREDICATE)
        self.assertEqual(_frozen(pat)[2], ("even",))

    def test_registry_head_passthrough(self):
        # explicit matcher heads still work: (REST r)
        pat = parse_case_pattern(_delim(
            _list(symbol("REST"), symbol("r"))))
        self.assertIsInstance(pat, REST)

    def test_app_with_pattern(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("f"), symbol("x"))))
        self.assertIsInstance(pat, APP)
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("x",))
        self.assertEqual(frozen[2], ("f",))

    def test_app_without_pattern(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("f"))))
        self.assertIsInstance(pat, APP)
        self.assertEqual(_frozen(pat)[2], ("f",))

    def test_app_registry_head(self):
        # the canonical matcher-surface spelling also works
        pat = parse_matcher(
            _list(symbol("APP"), symbol("f"), symbol("x")))
        self.assertIsInstance(pat, APP)

    def test_unknown_head_raises(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(_delim(
                _list(symbol("frobnicate"), symbol("a"))))


class TestCasePatternMatching(unittest.TestCase):
    """
    End-to-end: classified patterns thawed and run against values
    """

    def test_capture_binds_whole_value(self):
        pat = parse_case_pattern(_delim(symbol("x")))
        matched, caps = _match_value(pat, 42)
        self.assertTrue(matched)
        self.assertEqual(caps["x"], 42)

    def test_literal_hit_and_miss(self):
        pat = parse_case_pattern(_delim(5))
        self.assertTrue(_match_value(pat, 5)[0])
        self.assertFalse(_match_value(pat, 6)[0])

    def test_typed_hit_and_miss(self):
        pat = parse_case_pattern(
            _delim(symbol("x"), keyword("::"), symbol("int")))
        matched, caps = _match_value(pat, 7, int=int)
        self.assertTrue(matched)
        self.assertEqual(caps["x"], 7)
        self.assertFalse(_match_value(pat, "hi", int=int)[0])

    def test_sequence_with_rest(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), symbol("a"), symbol("b"),
                  keyword("*"), symbol("r"))))
        matched, caps = _match_value(pat, (1, 2, 3, 4))
        self.assertTrue(matched)
        self.assertEqual(caps["a"], 1)
        self.assertEqual(caps["b"], 2)
        self.assertEqual(caps["r"], (3, 4))

    def test_sequence_wrong_length_misses(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), symbol("a"), symbol("b"))))
        self.assertFalse(_match_value(pat, (1, 2, 3))[0])

    def test_strings_are_not_sequences(self):
        # PEP 634: str / bytes / bytearray never destructure into
        # characters under a sequence pattern
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), symbol("a"), symbol("b"),
                  keyword("*"), symbol("r"))))
        self.assertFalse(_match_value(pat, "plain")[0])
        self.assertFalse(_match_value(pat, b"plain")[0])
        self.assertFalse(_match_value(pat, bytearray(b"plain"))[0])

    def test_string_still_matches_as_atom(self):
        # excluded from sequences, but still a literal / capture / typed
        # subject as a whole value
        pat = parse_case_pattern(_delim("plain"))
        self.assertTrue(_match_value(pat, "plain")[0])
        pat = parse_case_pattern(
            _delim(symbol("s"), keyword("::"), symbol("str")))
        matched, caps = _match_value(pat, "plain", str=str)
        self.assertTrue(matched)
        self.assertEqual(caps["s"], "plain")

    def test_class_positional(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("item"), symbol("Point"),
                  symbol("a"), symbol("b"))))
        matched, caps = _match_value(pat, Point(3, 4), Point=Point)
        self.assertTrue(matched)
        self.assertEqual(caps["a"], 3)
        self.assertEqual(caps["b"], 4)
        self.assertFalse(_match_value(pat, "zzz", Point=Point)[0])

    def test_class_keyword_fields(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("item"), symbol("Point"),
                  keyword("x"), 0, symbol("y"))))
        matched, caps = _match_value(pat, Point(0, 9), Point=Point)
        self.assertTrue(matched)
        self.assertEqual(caps["y"], 9)
        self.assertFalse(_match_value(pat, Point(1, 9), Point=Point)[0])

    def test_or_alternation(self):
        pat = parse_case_pattern(_delim(_list(symbol("or"), 1, 2)))
        self.assertTrue(_match_value(pat, 1)[0])
        self.assertTrue(_match_value(pat, 2)[0])
        self.assertFalse(_match_value(pat, 3)[0])

    def test_predicate_hit_and_miss(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("?"), symbol("even"))))
        even = lambda v: v % 2 == 0  # noqa: E731
        self.assertTrue(_match_value(pat, 4, even=even)[0])
        self.assertFalse(_match_value(pat, 5, even=even)[0])

    def test_nested_class_in_sequence(self):
        # #[Point[a b] z]
        cls = _list(symbol("item"), symbol("Point"),
                    symbol("a"), symbol("b"))
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), cls, symbol("z"))))
        matched, caps = _match_value(
            pat, (Point(1, 2), 3), Point=Point)
        self.assertTrue(matched)
        self.assertEqual(caps["a"], 1)
        self.assertEqual(caps["b"], 2)
        self.assertEqual(caps["z"], 3)


def _dict_entry(key, val):
    """
    Build one reader-shaped mapping entry: (#tuple KEY VAL)
    """

    return _list(symbol("#tuple"), key, val)


def _dict_pattern(*entries):
    """
    Build a reader-shaped mapping pattern: (#dict (#tuple K V) ...)
    """

    return _list(symbol("#dict"), *entries)


class TestClassifyMap(unittest.TestCase):

    def test_literal_key_bind_value(self):
        # #{"kind": k}
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry("kind", symbol("k")))))
        self.assertIsInstance(pat, MAP)
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("k",))
        self.assertEqual(frozen[2], ())

    def test_extern_key(self):
        # #{key: v} -- symbol key resolves from the namespace
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry(symbol("key"), symbol("v")))))
        frozen = _frozen(pat)
        self.assertEqual(frozen[1], ("v",))
        self.assertEqual(frozen[2], ("key",))

    def test_empty_map(self):
        # #{} -- bare mapping check
        pat = parse_case_pattern(_delim(_dict_pattern()))
        self.assertIsInstance(pat, MAP)

    def test_malformed_entry_raises(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(_delim(
                _dict_pattern(_list(symbol("#tuple"), "only-key"))))

    def test_pair_key_raises(self):
        with self.assertRaises(ValueError):
            parse_case_pattern(_delim(
                _dict_pattern(_dict_entry(
                    _list(symbol("f"), 1), symbol("v")))))


class TestMapMatching(unittest.TestCase):
    """
    MAP: match a mapping element; each entry's key must be present with
    a value matching the sub-pattern; extra keys are ignored (PEP 634).
    """

    def test_literal_key_hit_and_bind(self):
        pat = parse_case_pattern(_delim(
            _dict_pattern(
                _dict_entry("kind", "job"),
                _dict_entry("url", symbol("u")))))
        matched, caps = _match_value(
            pat, {"kind": "job", "url": "http://x", "extra": 1})
        self.assertTrue(matched)
        self.assertEqual(caps["u"], "http://x")

    def test_missing_key_misses(self):
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry("kind", symbol("k")))))
        self.assertFalse(_match_value(pat, {"other": 1})[0])

    def test_value_pattern_misses(self):
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry("kind", "job"))))
        self.assertFalse(_match_value(pat, {"kind": "task"})[0])

    def test_extern_key_resolved_at_match_time(self):
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry(symbol("key"), symbol("v")))))
        matched, caps = _match_value(
            pat, {"dynamic": 9}, key="dynamic")
        self.assertTrue(matched)
        self.assertEqual(caps["v"], 9)
        self.assertFalse(
            _match_value(pat, {"dynamic": 9}, key="absent")[0])

    def test_non_mapping_misses(self):
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry("kind", symbol("k")))))
        self.assertFalse(_match_value(pat, (1, 2))[0])
        self.assertFalse(_match_value(pat, "kind")[0])
        self.assertFalse(_match_value(pat, 5)[0])

    def test_abc_mapping_accepted(self):
        import collections
        pat = parse_case_pattern(_delim(
            _dict_pattern(_dict_entry("a", symbol("v")))))
        subject = collections.ChainMap({"a": 3}, {"b": 4})
        matched, caps = _match_value(pat, subject)
        self.assertTrue(matched)
        self.assertEqual(caps["v"], 3)

    def test_empty_map_is_bare_mapping_check(self):
        pat = parse_case_pattern(_delim(_dict_pattern()))
        self.assertTrue(_match_value(pat, {})[0])
        self.assertTrue(_match_value(pat, {"any": 1})[0])
        self.assertFalse(_match_value(pat, (1, 2))[0])

    def test_nested_value_patterns(self):
        # #{"point": Point[a b] "tags": #[t *: r]}
        cls = _list(symbol("item"), symbol("Point"),
                    symbol("a"), symbol("b"))
        seq = _list(symbol("#list"), symbol("t"),
                    keyword("*"), symbol("r"))
        pat = parse_case_pattern(_delim(
            _dict_pattern(
                _dict_entry("point", cls),
                _dict_entry("tags", seq))))
        matched, caps = _match_value(
            pat, {"point": Point(1, 2), "tags": ("x", "y", "z")},
            Point=Point)
        self.assertTrue(matched)
        self.assertEqual(caps["a"], 1)
        self.assertEqual(caps["b"], 2)
        self.assertEqual(caps["t"], "x")
        self.assertEqual(caps["r"], ("y", "z"))

    def test_map_in_sequence(self):
        # #[#{"id": i} y]
        mp = _dict_pattern(_dict_entry("id", symbol("i")))
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), mp, symbol("y"))))
        matched, caps = _match_value(pat, ({"id": 7}, 8))
        self.assertTrue(matched)
        self.assertEqual(caps["i"], 7)
        self.assertEqual(caps["y"], 8)


class TestAppMatching(unittest.TestCase):
    """
    APP: apply an extern extractor to the element and match the result.
    The extractor is invoked through the demand path -- an awaitable
    result is driven to completion, a genuine async suspension raises
    KoskariSyncViolation, and a raising extractor propagates.
    """

    def test_extraction_and_bind(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("half"), symbol("h"))))
        matched, caps = _match_value(pat, 10, half=lambda v: v // 2)
        self.assertTrue(matched)
        self.assertEqual(caps["h"], 5)

    def test_extraction_result_miss(self):
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("half"), 4)))
        self.assertTrue(_match_value(pat, 8, half=lambda v: v // 2)[0])
        self.assertFalse(_match_value(pat, 10, half=lambda v: v // 2)[0])

    def test_extraction_result_destructured(self):
        # (app divmod3 #[q r])
        seq = _list(symbol("#list"), symbol("q"), symbol("r"))
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("divmod3"), seq)))
        matched, caps = _match_value(
            pat, 10, divmod3=lambda v: divmod(v, 3))
        self.assertTrue(matched)
        self.assertEqual(caps["q"], 3)
        self.assertEqual(caps["r"], 1)

    def test_app_in_sequence(self):
        # #[(app double d) y]
        appel = _list(symbol("app"), symbol("double"), symbol("d"))
        pat = parse_case_pattern(_delim(
            _list(symbol("#list"), appel, symbol("y"))))
        matched, caps = _match_value(
            pat, (4, 9), double=lambda v: v * 2)
        self.assertTrue(matched)
        self.assertEqual(caps["d"], 8)
        self.assertEqual(caps["y"], 9)

    def test_app_under_oneof(self):
        # (or (app half 3) (app half 5))
        orp = _list(
            symbol("or"),
            _list(symbol("app"), symbol("half"), 3),
            _list(symbol("app"), symbol("half"), 5))
        pat = parse_case_pattern(_delim(orp))
        half = lambda v: v // 2  # noqa: E731
        self.assertTrue(_match_value(pat, 6, half=half)[0])
        self.assertTrue(_match_value(pat, 10, half=half)[0])
        self.assertFalse(_match_value(pat, 8, half=half)[0])

    def test_async_extractor_driven_by_demand(self):
        # an immediately-completing coroutine result is stepped to its
        # value on the demand path
        async def afn(v):
            return v * 2
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("f"), symbol("x"))))
        m = thaw(_frozen(pat))
        r = m.match((5,), f=afn)
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["x"].datum, 10)

    def test_suspending_extractor_raises_sync_violation(self):
        import asyncio
        from koskari.lib import KoskariSyncViolation

        async def afn(v):
            await asyncio.sleep(0)
            return v
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("f"), symbol("x"))))
        m = thaw(_frozen(pat))
        with self.assertRaises(KoskariSyncViolation):
            m.match((5,), f=afn)

    def test_raising_extractor_propagates(self):
        def bad(v):
            raise RuntimeError("boom")
        pat = parse_case_pattern(_delim(
            _list(symbol("app"), symbol("f"), symbol("x"))))
        m = thaw(_frozen(pat))
        with self.assertRaises(RuntimeError):
            m.match((5,), f=bad)

    def test_async_predicate_driven_by_demand(self):
        async def apred(v):
            return v > 3
        pat = parse_case_pattern(_delim(
            _list(symbol("?"), symbol("p"))))
        m = thaw(_frozen(pat))
        self.assertTrue(m.match((5,), p=apred).matched)
        self.assertFalse(m.match((1,), p=apred).matched)


# The end.
