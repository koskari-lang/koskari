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

from koskari.lib import Datum, cons, nil
from koskari.matcher import thaw


pytestmark = pytest.mark.phase_infra


class Sentinel:
    pass


def _literal_matcher_tuple(const):
    """
    A one-node matcher whose entry is a literal test against const
    """

    nodes = (
        ("TEST", "LITERAL", 0),
    )
    return ("MATCHER", (), (), (const,), nodes, 0)


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


    def test_match_result_cycle_is_collected(self):
        def build():
            s = Sentinel()
            m = thaw(_literal_matcher_tuple(1))
            r = m.match((1,))
            r.captures["self"] = r
            r.captures["sentinel"] = s
            return s
        self._assert_collected(build)


    def test_matcher_cycle_is_collected(self):
        def build():
            s = Sentinel()
            m = thaw(_literal_matcher_tuple([s]))
            m.consts[0].append(m)
            return s
        self._assert_collected(build)


# The end.
