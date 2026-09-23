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
The matcher reads through a Mark for shape and hands the Mark back
inside the capture

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import Datum, DatumFrame, Mark, gensym, keyword, symbol
from koskari.matcher import (
    BIND, BINDING, CAPTURE, FORM, FORMALS, LITERAL_KW, LITERAL_SYM, MATCH,
    REST, SYMBOL, TEST_ANY, VALUE, repeat_capture)
from koskari.types import build_proper, unpack


pytestmark = pytest.mark.phase_infra


def _peel(val):
    return val.datum if isinstance(val, Datum) else val


class TestMarkedSubject(unittest.TestCase):


    def test_symbol_slot_accepts_mark_and_returns_it(self):
        m = MATCH(SYMBOL("name")).build()
        mk = Mark(symbol("tmp"), 1)
        result = m.match(build_proper(mk))
        self.assertTrue(result.matched)
        got = _peel(result.captures["name"])
        self.assertIsInstance(got, Mark)
        self.assertIs(got, mk)


    def test_symbol_literal_matches_through_mark(self):
        m = MATCH(LITERAL_SYM("match"), VALUE("inner")).build()
        mk = Mark(symbol("match"), 1)
        result = m.match(build_proper(mk, 1))
        self.assertTrue(result.matched)
        self.assertEqual(_peel(result.captures["inner"]), 1)


    def test_keyword_literal_matches_through_mark(self):
        m = MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")).build()
        mk = Mark(keyword("as"), 1)
        result = m.match(build_proper(1, mk, symbol("n")))
        self.assertTrue(result.matched)
        self.assertIs(_peel(result.captures["name"]), symbol("n"))


    def test_formals_keyword_headed_cell_keeps_mark(self):
        m = MATCH(FORMALS("f")).build()
        kw = Mark(keyword("argv"), 1)
        cell = build_proper(kw, None)
        result = m.match(build_proper(build_proper(cell)))
        self.assertTrue(result.matched)
        frame = _peel(result.captures["f"])
        pos = frame["positionals"]
        self.assertEqual(len(pos), 1)
        self.assertIs(_peel(pos[0]["kw"]), kw)
        self.assertIsNone(_peel(pos[0]["default"]))


    def test_formals_positional_keeps_mark(self):
        m = MATCH(FORMALS("f")).build()
        sx = Mark(symbol("x"), 1)
        result = m.match(build_proper(build_proper(sx)))
        self.assertTrue(result.matched)
        frame = _peel(result.captures["f"])
        self.assertIs(_peel(frame["positionals"][0]["name"]), sx)


class TestMarkedCapturePaths(unittest.TestCase):
    """
    Every capture path hands the Mark back: a multi-cell slice, a REST
    tail, a REPEAT row, a frame's source, and a cell boxed in a Datum
    around the Mark
    """


    def test_slice_capture_keeps_marks(self):
        m = MATCH(CAPTURE("two", MATCH(TEST_ANY(), TEST_ANY()))).build()
        a, b = Mark(symbol("a"), 1), Mark(symbol("b"), 1)
        result = m.match(build_proper(a, b))
        self.assertTrue(result.matched)
        got = [_peel(x) for x in result.captures["two"]]
        self.assertEqual(got, [a, b])


    def test_rest_tail_keeps_marks(self):
        m = MATCH(SYMBOL("head"), REST("tail")).build()
        a, b = Mark(symbol("a"), 1), Mark(symbol("b"), 1)
        result = m.match(build_proper(symbol("h"), a, b))
        self.assertTrue(result.matched)
        tail = list(unpack(_peel(result.captures["tail"])))
        self.assertEqual(tail, [a, b])


    def test_repeat_row_keeps_marks(self):
        m = MATCH(repeat_capture(
            "rows", FORM(MATCH(SYMBOL("n"), BIND("v"))))).build()
        a, b = Mark(symbol("a"), 1), Mark(symbol("b"), 1)
        result = m.match(build_proper(build_proper(a, 1), build_proper(b, 2)))
        self.assertTrue(result.matched)
        rows = result.captures["rows"]
        self.assertEqual([_peel(r["n"]) for r in rows], [a, b])


    def test_frame_source_keeps_marks(self):
        m = MATCH(BINDING("b")).build()
        name = Mark(symbol("t"), 1)
        cell = build_proper(name, 1)
        result = m.match(build_proper(cell))
        self.assertTrue(result.matched)
        frame = result.captures["b"]
        self.assertIsInstance(frame, DatumFrame)
        self.assertIs(_peel(frame["name"]), name)
        source_cell = list(unpack(frame.source))[0]
        self.assertIs(list(unpack(source_cell))[0], name)


    def test_datum_around_mark_yields_the_mark(self):
        m = MATCH(SYMBOL("name")).build()
        name = Mark(symbol("t"), 1)
        result = m.match(build_proper(Datum(name)))
        self.assertTrue(result.matched)
        self.assertIs(_peel(result.captures["name"]), name)


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
        result = m.match(build_proper(g))
        self.assertTrue(result.matched)
        got = _peel(result.captures["name"])
        self.assertIs(type(got), gensym)
        self.assertIs(got, g)


    def test_symbol_literal_matches_base(self):
        m = MATCH(LITERAL_SYM("match"), VALUE("inner")).build()
        result = m.match(build_proper(G("match"), 1))
        self.assertTrue(result.matched)
        self.assertEqual(_peel(result.captures["inner"]), 1)


    def test_symbol_literal_rejects_other_base(self):
        m = MATCH(LITERAL_SYM("match"), VALUE("inner")).build()
        result = m.match(build_proper(G("other"), 1))
        self.assertFalse(result.matched)


    def test_formals_positional_keeps_gensym(self):
        m = MATCH(FORMALS("f")).build()
        sx = G("x")
        result = m.match(build_proper(build_proper(sx)))
        self.assertTrue(result.matched)
        frame = _peel(result.captures["f"])
        self.assertIs(_peel(frame["positionals"][0]["name"]), sx)


    def test_slice_capture_keeps_gensyms(self):
        m = MATCH(CAPTURE("two", MATCH(TEST_ANY(), TEST_ANY()))).build()
        a, b = G("a"), G("b")
        result = m.match(build_proper(a, b))
        self.assertTrue(result.matched)
        got = [_peel(x) for x in result.captures["two"]]
        self.assertEqual(got, [a, b])


    def test_rest_tail_keeps_gensyms(self):
        m = MATCH(SYMBOL("head"), REST("tail")).build()
        a, b = G("a"), G("b")
        result = m.match(build_proper(symbol("h"), a, b))
        self.assertTrue(result.matched)
        tail = list(unpack(_peel(result.captures["tail"])))
        self.assertEqual(tail, [a, b])


    def test_repeat_row_keeps_gensyms(self):
        m = MATCH(repeat_capture(
            "rows", FORM(MATCH(SYMBOL("n"), BIND("v"))))).build()
        a, b = G("a"), G("b")
        result = m.match(build_proper(build_proper(a, 1), build_proper(b, 2)))
        self.assertTrue(result.matched)
        rows = result.captures["rows"]
        self.assertEqual([_peel(r["n"]) for r in rows], [a, b])


    def test_frame_source_keeps_gensyms(self):
        m = MATCH(BINDING("b")).build()
        name = G("t")
        cell = build_proper(name, 1)
        result = m.match(build_proper(cell))
        self.assertTrue(result.matched)
        frame = result.captures["b"]
        self.assertIsInstance(frame, DatumFrame)
        self.assertIs(_peel(frame["name"]), name)
        source_cell = list(unpack(frame.source))[0]
        self.assertIs(list(unpack(source_cell))[0], name)


    def test_datum_around_gensym_yields_the_gensym(self):
        m = MATCH(SYMBOL("name")).build()
        name = G("t")
        result = m.match(build_proper(Datum(name)))
        self.assertTrue(result.matched)
        self.assertIs(_peel(result.captures["name"]), name)


# The end.
