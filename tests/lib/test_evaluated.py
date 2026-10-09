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
tests.lib.test_evaluated

The evaluated marker type sits on the primary base chain of every
class that has it as a base: the marker itself cannot be subclassed,
and a class built on an adopter has that adopter as its __base__.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import KosPartial


pytestmark = pytest.mark.phase_lib


def add(a, b):
    return a + b


class Mixin:
    tag = "mixed"


class TestEvaluatedBase(unittest.TestCase):

    def test_the_marker_refuses_to_be_a_base(self):
        marker = KosPartial.__base__
        self.assertEqual("_KosEvaluated", marker.__name__)
        with self.assertRaises(TypeError):
            type("M", (Mixin, marker), {})

    def test_an_adopter_is_the_primary_base_beside_a_mixin(self):
        class M(Mixin, KosPartial):
            pass

        self.assertIs(KosPartial, M.__base__)
        p = M(add, 1)
        self.assertEqual("mixed", p.tag)
        self.assertEqual(3, p(2))


# The end.
