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
A gensym is a symbol to the matcher: a SYMBOL slot takes it, a symbol
literal matches its base, and every capture path hands it back

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import Datum, DatumFrame, demand, gensym, keyword, symbol
from koskari.matcher import (
    BIND, BINDING, CAPTURE, FORM, FORMALS, LITERAL, LITERAL_SYM, MATCH,
    REST, SYMBOL, TEST_ANY, VALUE, parse_matcher, repeat_capture)
from koskari.types import build_proper, unpack


pytestmark = pytest.mark.phase_infra


def _peel(val):
    return val.datum if isinstance(val, Datum) else val


def G(name, mark=1):
    return gensym(symbol(name), marks=mark)


class TestGensymSubject(unittest.TestCase):
    """
    A gensym is a symbol: a SYMBOL slot takes it with no peel, a
    symbol literal matches its base, and every capture path hands it
    back as itself
    """


    def test_symbol_slot_accepts_gensym_and_returns_it(self):
        m = MATCH(SYMBOL("name")).build()
        g = G("tmp")
        result = demand(m)(build_proper(g))
        self.assertIsNotNone(result)
        got = _peel(result["name"])
        self.assertIs(type(got), gensym)
        self.assertIs(got, g)


    def test_symbol_literal_matches_base(self):
        m = MATCH(LITERAL_SYM("match"), VALUE("inner")).build()
        result = demand(m)(build_proper(G("match"), 1))
        self.assertIsNotNone(result)
        self.assertEqual(_peel(result["inner"]), 1)


    def test_symbol_literal_rejects_other_base(self):
        m = MATCH(LITERAL_SYM("match"), VALUE("inner")).build()
        result = demand(m)(build_proper(G("other"), 1))
        self.assertIsNone(result)


    def test_literal_built_from_gensym_holds_the_base(self):
        m = MATCH(LITERAL(G("match")), VALUE("inner")).build()
        result = demand(m)(build_proper(symbol("match"), 1))
        self.assertIsNotNone(result)
        self.assertEqual(_peel(result["inner"]), 1)


    def test_val_payload_from_gensym_matches_the_base(self):
        pattern = build_proper(
            symbol("MATCH"),
            build_proper(symbol("inner"), keyword("val"), G("match")))
        m = parse_matcher(pattern).build()
        result = demand(m)(build_proper(symbol("match")))
        self.assertIsNotNone(result)
        self.assertIs(_peel(result["inner"]), symbol("match"))


    def test_formals_positional_keeps_gensym(self):
        m = MATCH(FORMALS("f")).build()
        sx = G("x")
        result = demand(m)(build_proper(build_proper(sx)))
        self.assertIsNotNone(result)
        frame = _peel(result["f"])
        self.assertIs(_peel(frame["positionals"][0]["name"]), sx)


    def test_slice_capture_keeps_gensyms(self):
        m = MATCH(CAPTURE("two", MATCH(TEST_ANY(), TEST_ANY()))).build()
        a, b = G("a"), G("b")
        result = demand(m)(build_proper(a, b))
        self.assertIsNotNone(result)
        got = [_peel(x) for x in result["two"]]
        self.assertEqual(got, [a, b])


    def test_rest_tail_keeps_gensyms(self):
        m = MATCH(SYMBOL("head"), REST("tail")).build()
        a, b = G("a"), G("b")
        result = demand(m)(build_proper(symbol("h"), a, b))
        self.assertIsNotNone(result)
        tail = list(unpack(_peel(result["tail"])))
        self.assertEqual(tail, [a, b])


    def test_repeat_row_keeps_gensyms(self):
        m = MATCH(repeat_capture(
            "rows", FORM(MATCH(SYMBOL("n"), BIND("v"))))).build()
        a, b = G("a"), G("b")
        result = demand(m)(build_proper(build_proper(a, 1), build_proper(b, 2)))
        self.assertIsNotNone(result)
        rows = result["rows"]
        self.assertEqual([_peel(r["n"]) for r in rows], [a, b])


    def test_frame_source_keeps_gensyms(self):
        m = MATCH(BINDING("b")).build()
        name = G("t")
        cell = build_proper(name, 1)
        result = demand(m)(build_proper(cell))
        self.assertIsNotNone(result)
        frame = result["b"]
        self.assertIsInstance(frame, DatumFrame)
        self.assertIs(_peel(frame["name"]), name)
        source_cell = list(unpack(frame.source))[0]
        self.assertIs(list(unpack(source_cell))[0], name)


    def test_datum_around_gensym_yields_the_gensym(self):
        m = MATCH(SYMBOL("name")).build()
        name = G("t")
        result = demand(m)(build_proper(Datum(name)))
        self.assertIsNotNone(result)
        self.assertIs(_peel(result["name"]), name)


# The end.
