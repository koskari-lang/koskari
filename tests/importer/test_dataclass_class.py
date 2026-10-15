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
tests.importer.test_dataclass_class

The dataclass decorator applied to a class: a subclass that adds no
fields is (class Child [Parent]) under the decorator, which inherits
the parent's fields rather than reading its supers as a member form.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_basics


SRC = """
import-from dataclasses dataclass
(decorate [dataclass] (class Parent [] (var x :: int 1)))
(decorate [dataclass] (class Child [Parent]))
(decorate [dataclass] (class Grand [Child] (var y :: int 2)))
"""

LAYOUT_SRC = """
import-from dataclasses dataclass

@dataclass
class Point:
  var x :: int 0
  var y :: int 0

@dataclass
class Point3 [Point]:
  var z :: int 0
"""


class TestDataclassClass(unittest.IsolatedAsyncioTestCase):

    async def load(self, src=SRC):
        module = new_module("dataclass_class_subject")
        vars(module)["__file__"] = "<dataclass_class_subject>"
        await load_module_source_async(module, src)
        return vars(module)

    async def test_subclass_with_no_members_keeps_its_base(self):
        v = await self.load()
        self.assertEqual(v["Child"].__bases__, (v["Parent"],))
        self.assertEqual(list(v["Child"].__dataclass_fields__), ["x"])
        child = v["Child"](5)
        self.assertEqual(child.x, 5)
        self.assertIsInstance(child, v["Parent"])

    async def test_subclass_with_members_adds_to_the_base(self):
        v = await self.load()
        self.assertEqual(v["Grand"].__bases__, (v["Child"],))
        self.assertEqual(list(v["Grand"].__dataclass_fields__), ["x", "y"])
        self.assertEqual(v["Grand"](7, 8).y, 8)

    async def test_layout_header_may_elide_supers(self):
        v = await self.load(LAYOUT_SRC)
        self.assertEqual(v["Point"].__bases__, (object,))
        self.assertEqual(list(v["Point"].__dataclass_fields__), ["x", "y"])
        self.assertEqual(v["Point3"].__bases__, (v["Point"],))
        self.assertEqual(v["Point3"](1, 2, 3).z, 3)


# The end.
