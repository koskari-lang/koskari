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
Tests that reference cycles through the matcher's objects are collected

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import gc
import unittest
import weakref

import pytest

from koskari.lib import Datum, cons, demand, nil
from koskari.matcher import CAPTURE, LITERAL, MATCH


pytestmark = pytest.mark.phase_infra


class Sentinel:
    pass


class TestMatcherGCCycles(unittest.TestCase):

    def _assert_collected(self, build):
        """
        build returns a sentinel held only through the cycle it made
        """

        ref = weakref.ref(build())
        gc.collect()
        self.assertIsNone(ref())


    def test_datum_cycle_is_collected(self):
        def build():
            s = Sentinel()
            d = Datum([s])
            d.datum.append(d)
            return s
        self._assert_collected(build)


    def test_datum_iter_cycle_is_collected(self):
        def build():
            s = Sentinel()
            box = [s]
            it = iter(Datum(cons(box, nil)))
            box.append(it)
            return s
        self._assert_collected(build)


    def test_captures_cycle_is_collected(self):
        def build():
            s = Sentinel()
            m = MATCH(CAPTURE("x", LITERAL(1))).build()
            r = demand(m)((1,))
            r["self"] = r
            r["sentinel"] = s
            return s
        self._assert_collected(build)


    def test_list_literal_is_refused_at_build(self):
        """
        A matcher cannot hold a list literal, which could route a cycle
        through its constants: the build refuses it
        """

        with self.assertRaises(TypeError) as caught:
            MATCH(LITERAL([Sentinel()])).build()
        self.assertIn("list", str(caught.exception))


# The end.
