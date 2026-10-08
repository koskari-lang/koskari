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

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


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


class TestAttrFieldCache(CevalTest):
    """
    Tests for the field cache on ATTR and ATTR_STORE: a stack receiver
    takes the same slot and inline-value hits as the named forms
    """

    async def test_attr_slots_load_repeats_through_cache(self):
        """
        ATTR on a __slots__ member answers the same on the first
        (populating) and second (cached) execution
        """

        class Box(object):
            __slots__ = ("value",)
        box = Box()
        box.value = "slotted"
        code, ns = build_expr([
            ["CONST", box],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), "slotted")
        self.assertEqual(await fn(), "slotted")


    async def test_attr_instance_load_repeats_and_sees_rebinding(self):
        """
        ATTR on an instance attribute answers the current binding on
        every execution
        """

        class Box(object):
            def __init__(self):
                self.value = 1
        box = Box()
        code, ns = build_expr([
            ["CONST", box],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), 1)
        self.assertEqual(await fn(), 1)
        box.value = 2
        self.assertEqual(await fn(), 2)


    async def test_attr_polymorphic_site_answers_each_receiver(self):
        """
        ATTR at one site over receivers of different types answers
        each receiver's own attribute
        """

        class Slotted(object):
            __slots__ = ("value",)

        class Plain(object):
            def __init__(self):
                self.value = "plain"
        slotted = Slotted()
        slotted.value = "slotted"
        code, ns = build_expr([
            ["VAR", "obj"],
            ["ATTR", "value"],
            ["RETURN", 1],
        ], values={"obj": None})
        fn = make_callable(code, ns)
        for _ in range(3):
            for obj, expect in ((slotted, "slotted"), (Plain(), "plain")):
                ns["obj"] = obj
                self.assertEqual(await fn(), expect)


    async def test_attr_property_takes_generic_path(self):
        """
        ATTR on a property computes through getattr each time
        """

        class Box(object):
            count = 0

            @property
            def value(self):
                Box.count += 1
                return Box.count
        box = Box()
        code, ns = build_expr([
            ["CONST", box],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), 1)
        self.assertEqual(await fn(), 2)


    async def test_attr_unset_slot_falls_back_to_getattr(self):
        """
        ATTR on an unset __slots__ member runs __getattr__
        """

        class Box(object):
            __slots__ = ("value",)

            def __getattr__(self, name):
                return "fallback:" + name
        box = Box()
        code, ns = build_expr([
            ["CONST", box],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), "fallback:value")
        box.value = "set"
        self.assertEqual(await fn(), "set")


    async def test_attr_missing_attr_raises(self):
        """
        ATTR on a missing attribute raises AttributeError on every
        execution
        """

        class Box(object):
            pass
        code, ns = build_expr([
            ["CONST", Box()],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(AttributeError):
            await fn()
        with self.assertRaises(AttributeError):
            await fn()


    async def test_attr_store_slots_repeats_and_keeps_refcount(self):
        """
        ATTR_STORE into a __slots__ member twice leaves the value with
        exactly the member's reference
        """

        import sys

        class Box(object):
            __slots__ = ("value",)
        box = Box()
        payload = object()
        before = sys.getrefcount(payload)
        code, ns = build_expr([
            ["CONST", box],
            ["CONST", payload],
            ["ATTR_STORE", "value"],
            ["CONST", None],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        await fn()
        await fn()
        self.assertIs(box.value, payload)
        del box.value
        del code, ns, fn
        self.assertEqual(sys.getrefcount(payload), before)


    async def test_attr_store_instance_repeats_and_keeps_refcount(self):
        """
        ATTR_STORE into an instance attribute twice leaves the value
        with exactly the instance's reference and the receiver released
        """

        import sys

        class Box(object):
            def __init__(self):
                self.value = None
        box = Box()
        payload = object()
        before = sys.getrefcount(payload)
        box_before = sys.getrefcount(box)
        code, ns = build_expr([
            ["CONST", box],
            ["CONST", payload],
            ["ATTR_STORE", "value"],
            ["CONST", None],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        await fn()
        await fn()
        self.assertIs(box.value, payload)
        box.value = None
        del code, ns, fn
        self.assertEqual(sys.getrefcount(payload), before)
        self.assertEqual(sys.getrefcount(box), box_before)


    async def test_attr_store_setattr_override_takes_generic_path(self):
        """
        ATTR_STORE on a type with __setattr__ goes through it
        """

        seen = []

        class Box(object):
            def __setattr__(self, name, value):
                seen.append((name, value))
        box = Box()
        code, ns = build_expr([
            ["CONST", box],
            ["CONST", 5],
            ["ATTR_STORE", "value"],
            ["CONST", None],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        await fn()
        await fn()
        self.assertEqual(seen, [("value", 5), ("value", 5)])


    async def test_attr_store_rejected_raises_each_time(self):
        """
        ATTR_STORE on a receiver that refuses the attribute raises
        AttributeError on every execution
        """

        code, ns = build_expr([
            ["CONST", object()],
            ["CONST", 5],
            ["ATTR_STORE", "value"],
            ["CONST", None],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(AttributeError):
            await fn()
        with self.assertRaises(AttributeError):
            await fn()


# The end.
