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
tests.types.test_is_const

is_const admits the constant pool's own kinds by exact type: a gensym
and builtin tuples of them are constants; an enum member, a str
subclass and a namedtuple are not, whatever builtin they derive from

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import enum
import unittest
from collections import namedtuple

import pytest

from koskari.lib import gensym, keyword, nil, symbol
from koskari.types import is_const


pytestmark = pytest.mark.phase_lib


class Level(enum.IntEnum):
    LOW = 1


class Name(str):
    pass


Point = namedtuple("Point", "x y")


class TestIsConst(unittest.TestCase):

    def test_builtin_kinds(self):
        for value in (1, "a", b"a", 1.5, None, True, symbol("x"),
                      keyword("k"), nil, gensym("g")):
            self.assertTrue(is_const(value), value)


    def test_nested_builtin_containers(self):
        self.assertTrue(is_const((1, ("a", frozenset({2})))))


    def test_refuses_subclass_values(self):
        for value in (Level.LOW, Name("a"), Point(1, 2)):
            self.assertFalse(is_const(value), value)


    def test_refuses_container_holding_subclass(self):
        self.assertFalse(is_const((1, Level.LOW)))


    def test_refuses_mutable_containers(self):
        self.assertFalse(is_const([1, 2]))


# The end.
