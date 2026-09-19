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
Unit tests for the Mark type

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import Mark, keyword, kos_repr, symbol


pytestmark = pytest.mark.phase_lib


class TestMark(unittest.TestCase):


    def test_symbol_key(self):
        m = Mark(symbol("tmp"), 1)
        self.assertIs(m.datum, symbol("tmp"))
        self.assertEqual(m.marks, (1,))
        self.assertEqual(m.key, "tmp#1")
        self.assertEqual(str(m), "tmp#1")


    def test_keyword_key_is_the_parameter_name(self):
        m = Mark(keyword("argv"), 1)
        self.assertIs(m.datum, keyword("argv"))
        self.assertEqual(str(m), "argv#1")


    def test_marks_tuple_in_order_hex(self):
        m = Mark(symbol("y"), (1, 255))
        self.assertEqual(m.marks, (1, 255))
        self.assertEqual(str(m), "y#1#ff")


    def test_marks_accepts_list(self):
        m = Mark(symbol("y"), [3, 4])
        self.assertEqual(m.marks, (3, 4))


    def test_rejects_non_atom_datum(self):
        with self.assertRaises(TypeError):
            Mark("tmp", 1)


    def test_rejects_empty_marks(self):
        with self.assertRaises(ValueError):
            Mark(symbol("tmp"), ())


    def test_rejects_non_int_marks(self):
        with self.assertRaises(TypeError):
            Mark(symbol("tmp"), ("a",))


    def test_equality_by_datum_and_marks(self):
        a = Mark(symbol("t"), (1,))
        b = Mark(symbol("t"), (1,))
        c = Mark(symbol("t"), (2,))
        d = Mark(symbol("u"), (1,))
        self.assertEqual(a, b)
        self.assertEqual(hash(a), hash(b))
        self.assertNotEqual(a, c)
        self.assertNotEqual(a, d)
        self.assertNotEqual(a, symbol("t"))


    def test_usable_as_dict_key(self):
        a = Mark(symbol("t"), (1,))
        d = {a: 1}
        self.assertEqual(d[Mark(symbol("t"), (1,))], 1)


    def test_repr_and_kos_repr(self):
        m = Mark(symbol("t"), 1)
        self.assertEqual(repr(m), "<koskari mark t#1>")
        self.assertEqual(kos_repr(m), "t")
        self.assertEqual(kos_repr(Mark(keyword("else"), 1)), "else:")


# The end.
