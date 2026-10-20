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
tests.matcher.test_group_rows

A named GROUP keeps its captures once it has matched: a REPEAT row that
holds one is harvested intact, and a backtrack past the group discards
the whole frame rather than emptying one a harvested row still holds.
"""


import unittest

import pytest

from koskari.lib import Datum, demand
from koskari.matcher import (
    CAPTURE, GROUP, LIST, LITERAL, MATCH, ONEOF, REPEAT, VALUE, repeat_capture)


pytestmark = pytest.mark.phase_infra


def plain(value):
    return value.datum if isinstance(value, Datum) else value


class TestGroupRows(unittest.TestCase):

    def test_named_group_inside_a_repeat_row_keeps_its_captures(self):
        pat = repeat_capture(
            "rows", LIST(VALUE("head"), GROUP("g", VALUE("a"), VALUE("b"))))
        result = demand(pat.build())(((1, 2, 3), (4, 5, 6)))
        self.assertIsNotNone(result)
        rows = plain(result["rows"])
        self.assertEqual(2, len(rows))
        self.assertEqual(1, plain(rows[0]["head"]))
        self.assertEqual({"a": 2, "b": 3},
                         {k: plain(v) for k, v in rows[0]["g"].items()})
        self.assertEqual({"a": 5, "b": 6},
                         {k: plain(v) for k, v in rows[1]["g"].items()})


    def test_named_group_as_the_whole_repeat_row(self):
        pat = repeat_capture("rows", GROUP("g", VALUE("a")))
        result = demand(pat.build())((7, 8))
        self.assertIsNotNone(result)
        rows = plain(result["rows"])
        self.assertEqual([{"a": 7}, {"a": 8}],
                         [{k: plain(v) for k, v in row["g"].items()}
                          for row in rows])


    def test_backtrack_past_a_group_discards_its_frame(self):
        pat = MATCH(ONEOF(
            MATCH(GROUP("g", VALUE("a")), LITERAL("never")),
            MATCH(VALUE("x"), VALUE("y"))))
        result = demand(pat.build())((1, 2))
        self.assertIsNotNone(result)
        self.assertNotIn("g", result)
        self.assertEqual(1, plain(result["x"]))
        self.assertEqual(2, plain(result["y"]))


    def test_group_captured_outside_a_repeat(self):
        pat = MATCH(VALUE("head"), CAPTURE("g", GROUP(None, VALUE("a"))))
        result = demand(pat.build())((1, 2))
        self.assertIsNotNone(result)
        self.assertEqual({"a": 2},
                         {k: plain(v) for k, v in result["g"].items()})


# The end.
