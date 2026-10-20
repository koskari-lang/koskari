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
tests.compile.specials.test_class_mro_entries

A class base that is not a type but defines __mro_entries__, such as
Generic[T], NamedTuple or TypedDict, is replaced by what that method
returns before the metaclass is called, and the bases as written are
kept on the class as __orig_bases__, the way a Python class statement
builds its class.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from typing import Generic, NamedTuple, TypedDict, TypeVar

import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


T = TypeVar("T")


class TestClassMroEntries(SpecialsTestCase):

    async def test_generic_base(self):
        src_str = """
        (build-class Box (GenericT)
          (define [__init__ self v] (setattr self "v" v)))
        """
        expr, mod = self.compile_expr(
            src_str, GenericT=Generic[T], setattr=setattr)
        box = await expr()
        self.assertEqual(1, box(1).v)
        self.assertEqual((Generic[T],), box.__orig_bases__)
        self.assertIn(Generic, box.__mro__)


    async def test_named_tuple_base(self):
        src_str = """
        (build-class Point (NamedTuple)
          (var __annotations__ #{"x": int "y": int}))
        """
        expr, mod = self.compile_expr(
            src_str, NamedTuple=NamedTuple, int=int)
        point = await expr()
        p = point(1, 2)
        self.assertEqual((1, 2), tuple(p))
        self.assertEqual(2, p.y)
        self.assertEqual((NamedTuple,), point.__orig_bases__)


    async def test_typed_dict_base(self):
        src_str = """
        (build-class Movie (TypedDict)
          (var __annotations__ #{"title": str}))
        """
        expr, mod = self.compile_expr(src_str, TypedDict=TypedDict, str=str)
        movie = await expr()
        self.assertEqual({"title": "x"}, movie(title="x"))
        self.assertEqual({"title": str}, movie.__annotations__)


    async def test_plain_base_leaves_no_orig_bases(self):
        src_str = """
        (build-class C (Base) 5)
        """
        expr, mod = self.compile_expr(src_str, Base=object)
        cls = await expr()
        self.assertEqual((object,), cls.__bases__)
        self.assertFalse(hasattr(cls, "__orig_bases__"))


# The end.
