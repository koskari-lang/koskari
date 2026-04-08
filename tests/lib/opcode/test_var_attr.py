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
Tests for VAR_ATTR, VAR_ATTR_CLEAR, VAR_ATTR_REF, VAR_ATTR_STORE and
FAST_ATTR, FAST_ATTR_CLEAR, FAST_ATTR_REF, FAST_ATTR_STORE opcodes.

EXTRA schema (VAR, CONST, NOARG, NOARG) or (FASTVAR, CONST, NOARG, NOARG).
"""

from koskari.lib import FormalsSpec

from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestVarAttrOpcodes(CevalTest):
    """
    Tests for VAR_ATTR, VAR_ATTR_CLEAR, VAR_ATTR_REF, VAR_ATTR_STORE
    """

    async def test_var_attr_getattr(self):
        """
        VAR_ATTR pushes getattr(obj, attr) where obj from namespace
        """

        class Box(object):
            value = 3
        box = Box()
        code, ns = build_expr([
            ["VAR_ATTR", ("obj", "value", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": box})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 3)


    async def test_var_attr_store_and_getattr(self):
        """
        VAR_ATTR_STORE sets attribute; VAR_ATTR loads it
        """

        class Box(object):
            pass
        obj = Box()
        code, ns = build_expr([
            ["CONST", 99],
            ["VAR_ATTR_STORE", ("obj", "value", 0, 0)],
            ["VAR_ATTR", ("obj", "value", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": obj})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)
        self.assertEqual(obj.value, 99)


    async def test_var_attr_clear_delattr(self):
        """
        VAR_ATTR_CLEAR deletes attribute
        """

        class Box(object):
            pass
        obj = Box()
        obj.x = 1
        code, ns = build_expr([
            ["VAR_ATTR_CLEAR", ("obj", "x", 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
        ], values={"obj": obj})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)
        self.assertFalse(hasattr(obj, "x"))


    async def test_var_attr_ref_pushes_attribute_ref(self):
        """
        VAR_ATTR_REF pushes AttributeRef for obj.attr
        """

        from koskari.lib import AttributeRef
        class Box(object):
            pass
        obj = Box()
        obj.foo = "bar"
        code, ns = build_expr([
            ["VAR_ATTR_REF", ("obj", "foo", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": obj})
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, AttributeRef)
        self.assertEqual(result.deref(), "bar")


    async def test_var_attr_missing_var_raises(self):
        """
        VAR_ATTR when var undefined raises
        """

        code, ns = build_expr([
            ["VAR_ATTR", ("nonexistent", "value", 0, 0)],
            ["RETURN", 1],
        ])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_var_attr_missing_attr_raises(self):
        """
        VAR_ATTR when attr missing on obj raises AttributeError
        """

        code, ns = build_expr([
            ["VAR_ATTR", ("obj", "nonexistent", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": 42})
        with self.assertRaises(AttributeError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("int", str(ctx.exception))
        self.assertIn("nonexistent", str(ctx.exception))


class TestFastAttrOpcodes(CevalTest):
    """
    Tests for FAST_ATTR, FAST_ATTR_CLEAR, FAST_ATTR_REF, FAST_ATTR_STORE.
    Uses Function with FormalsSpec so frame has fast locals.
    """

    async def test_fast_attr_getattr(self):
        """
        FAST_ATTR pushes getattr(obj, attr) where obj from slot 0
        """

        class Box(object):
            value = 3
        box = Box()
        code = build_code([
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(box)
        self.assertEqual(result, 3)


    async def test_fast_attr_store_and_getattr(self):
        """
        FAST_ATTR_STORE sets attribute; FAST_ATTR loads it
        """

        class Box(object):
            pass
        obj = Box()
        code = build_code([
            ["CONST", 99],
            ["FAST_ATTR_STORE", (0, "value", 0, 0)],
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(obj)
        self.assertEqual(result, 99)
        self.assertEqual(obj.value, 99)


    async def test_fast_attr_clear_delattr(self):
        """
        FAST_ATTR_CLEAR deletes attribute
        """

        class Box(object):
            pass
        obj = Box()
        obj.x = 1
        code = build_code([
            ["FAST_ATTR_CLEAR", (0, "x", 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(obj)
        self.assertEqual(result, 42)
        self.assertFalse(hasattr(obj, "x"))


    async def test_fast_attr_ref_pushes_attribute_ref(self):
        """
        FAST_ATTR_REF pushes AttributeRef for obj.attr
        """

        from koskari.lib import AttributeRef
        class Box(object):
            pass
        obj = Box()
        obj.foo = "bar"
        code = build_code([
            ["FAST_ATTR_REF", (0, "foo", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(obj)
        self.assertIsInstance(result, AttributeRef)
        self.assertEqual(result.deref(), "bar")


    async def test_fast_attr_missing_attr_raises(self):
        """
        FAST_ATTR when attr missing on obj raises AttributeError
        """

        code = build_code([
            ["FAST_ATTR", (0, "nonexistent", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        with self.assertRaises(AttributeError) as ctx:
            await fn(42)
        self.assertIn("int", str(ctx.exception))
        self.assertIn("nonexistent", str(ctx.exception))


# The end.
