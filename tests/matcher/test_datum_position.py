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
Tests that KosPair spine-cell positions propagate into Datum captures

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest
import weakref

import pytest

from koskari.lib import Datum, cdr, cons, demand, nil, symbol
from koskari.matcher import CAPTURE, MATCH, REST, VALUE, DatumFrame
from koskari.types import build_proper, set_position


pytestmark = pytest.mark.phase_infra


def _head_tail_matcher():
    """
    A matcher: CAPTURE head, then the REST captured as tail
    """

    return MATCH(CAPTURE("head"), REST("tail")).build()


class TestDatumPosition(unittest.TestCase):

    def test_capture_inherits_spine_cell_position(self):
        """
        CAPTURE and REST slots inherit position from their spine cells
        """

        pos_head = (4, 1)
        pos_tail = (5, 2)
        subj = cons(1, 2, 3, nil)
        subj.set_position(pos_head)
        cdr(subj).set_position(pos_tail)

        result = demand(_head_tail_matcher())(subj)
        self.assertIsNotNone(result)

        head = result["head"]
        self.assertIsInstance(head, Datum)
        self.assertEqual(head.datum, 1)
        self.assertEqual(head.position, pos_head)

        tail = result["tail"]
        self.assertIsInstance(tail, Datum)
        self.assertEqual(tail.datum, cons(2, 3, nil))
        self.assertEqual(tail.position, pos_tail)


    def test_value_capture_in_datum_frame_carries_cell_position(self):
        """
        VALUE capture inside a root DatumFrame carries the cell position
        """

        pos = (2, 8)
        value = symbol("a")
        source = build_proper(value)
        set_position(source, pos)
        matcher = MATCH(VALUE("x")).build()

        result = demand(matcher)(source)
        self.assertIsNotNone(result)
        self.assertIsInstance(result, DatumFrame)
        self.assertEqual(result.position, pos)

        cap = result["x"]
        self.assertIsInstance(cap, Datum)
        self.assertIs(cap.datum, value)
        self.assertEqual(cap.position, pos)


    def test_datum_position_none_when_cell_unset(self):
        """
        Datum.position is None when the matched spine cell has no position
        """

        subj = cons(1, 2, nil)
        result = demand(_head_tail_matcher())(subj)
        self.assertIsNotNone(result)
        self.assertIsNone(result["head"].position)


class TestDatumFrameConstruction(unittest.TestCase):
    """
    A DatumFrame built from Python is as well formed as one the matcher
    builds: source and position are None, not unset
    """

    def test_empty_frame_fields(self):
        frame = DatumFrame()
        self.assertIsNone(frame.source)
        self.assertIsNone(frame.position)
        self.assertEqual(dict(frame), {})
        self.assertIn("source=None", repr(frame))
        self.assertIn("position=None", repr(frame))

    def test_dict_style_construction_keeps_fields(self):
        frame = DatumFrame({"a": 1}, b=2)
        self.assertEqual(dict(frame), {"a": 1, "b": 2})
        self.assertIsNone(frame.source)
        self.assertIsNone(frame.position)
        self.assertEqual(frame["a"], 1)

    def test_fields_survive_dict_mutation_and_copy(self):
        frame = DatumFrame()
        frame["x"] = symbol("x")
        frame.clear()
        self.assertIsNone(frame.source)
        self.assertIsNone(frame.position)
        self.assertIsNone(DatumFrame(frame).position)


class Elem:
    pass


class TestDatumIteration(unittest.TestCase):
    """
    Iterating a Datum over a pair spine keeps each car alive when the
    iterator is the only holder of the spine
    """

    def test_car_survives_when_iterator_owns_the_spine(self):
        first = Elem()
        second = Elem()
        alive_first = weakref.ref(first)
        alive_second = weakref.ref(second)

        it = iter(Datum(cons(first, second, nil)))
        del first, second

        got = next(it)
        self.assertIsNotNone(alive_first())
        self.assertIs(got.datum, alive_first())

        got = next(it)
        self.assertIsNotNone(alive_second())
        self.assertIs(got.datum, alive_second())

        with self.assertRaises(StopIteration):
            next(it)

    def test_iteration_yields_every_element_in_order(self):
        out = [d.datum for d in Datum(cons(1, 2, 3, nil))]
        self.assertEqual([1, 2, 3], out)


# The end.
