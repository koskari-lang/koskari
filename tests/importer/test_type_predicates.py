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
The runtime instance? and subclass? builtins take one or more types
after the subject, open a tuple operand one level, and refuse a tuple
inside a tuple

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.builtins as builtins


pytestmark = pytest.mark.phase_basics


instance_p = getattr(builtins, "instance?")
subclass_p = getattr(builtins, "subclass?")


class TestInstancePredicate(unittest.TestCase):

    def test_one_type(self):
        self.assertIs(instance_p(1, int), True)
        self.assertIs(instance_p("x", int), False)


    def test_several_types(self):
        self.assertIs(instance_p("x", int, str), True)
        self.assertIs(instance_p(1.5, int, str), False)


    def test_tuple_operand_opens_one_level(self):
        self.assertIs(instance_p("x", (int, float), str), True)
        self.assertIs(instance_p("x", (int, str)), True)
        self.assertIs(instance_p("x", ()), False)


    def test_nested_tuple_raises(self):
        with self.assertRaises(TypeError):
            instance_p("x", ((int,), str))


    def test_needs_a_type(self):
        with self.assertRaises(TypeError):
            instance_p("x")


class TestSubclassPredicate(unittest.TestCase):

    def test_several_bases(self):
        self.assertIs(subclass_p(bool, str, int), True)
        self.assertIs(subclass_p(float, str, int), False)


    def test_tuple_operand_opens_one_level(self):
        self.assertIs(subclass_p(bool, (str, bytes), (float, int)), True)


    def test_nested_tuple_raises(self):
        with self.assertRaises(TypeError):
            subclass_p(bool, ((int,), str))


    def test_needs_a_base(self):
        with self.assertRaises(TypeError):
            subclass_p(bool)


# The end.
