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

from koskari.lib import Datum, Mark, keyword, symbol
from koskari.matcher import (
    FORMALS, LITERAL_KW, LITERAL_SYM, MATCH, SYMBOL, VALUE)
from koskari.types import build_proper


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


# The end.
