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
Tests for ATTR, ATTR_STORE opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestAttrOpcodes(CevalTest):
    """
    Tests for ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE
    """

    async def test_attr_getattr(self):
        """
        ATTR pushes getattr(TOS, name)
        """

        class Box(object):
            value = 3
        code, ns = build_expr([
            ["CONST", Box],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 3)


    async def test_attr_store_and_getattr(self):
        """
        ATTR_STORE sets attribute; ATTR loads it
        """

        class Box(object):
            pass
        obj = Box()
        code, ns = build_expr([
            ["CONST", obj],
            ["CONST", 99],
            ["ATTR_STORE", "value"],
            ["CONST", obj],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)
        self.assertEqual(obj.value, 99)


    async def test_attr_clear_delattr(self):
        """
        ATTR_CLEAR deletes attribute
        """

        class Box(object):
            pass
        obj = Box()
        obj.x = 1
        code, ns = build_expr([
            ["CONST", obj],
            ["ATTR_CLEAR", "x"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)
        self.assertFalse(hasattr(obj, "x"))


    async def test_attr_ref_pushes_attribute_ref(self):
        """
        ATTR_REF pushes AttributeRef for TOS.attr
        """

        from koskari.lib import AttributeRef
        class Box(object):
            pass
        obj = Box()
        obj.foo = "bar"
        code, ns = build_expr([
            ["CONST", obj],
            ["ATTR_REF", "foo"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, AttributeRef)
        self.assertEqual(result.deref(), "bar")


# The end.
