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

import pytest

from koskari.lib import FormalsSpec, Ref
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


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


    async def test_fast_attr_ref_keeps_receiver_refcount(self):
        """
        FAST_ATTR_REF leaves the receiver with only the namespace's and
        the ref's references
        """

        import sys

        class Box(object):
            pass
        obj = Box()
        obj.foo = "bar"
        before = sys.getrefcount(obj)
        code, ns = build_expr([
            ["FAST_ATTR_REF", (0, "foo", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), values={"obj": obj})
        fn = make_callable(code, ns)
        ref = await fn()
        self.assertEqual(ref.deref(), "bar")
        del ref, code, ns, fn
        self.assertEqual(sys.getrefcount(obj), before)


    async def test_fast_attr_ref_unbound_slot_raises(self):
        code, ns = build_expr([
            ["FAST_ATTR_REF", (0, "foo", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_fast_attr_ref_through_computing_ref(self):
        """
        FAST_ATTR_REF on a slot bound to a Ref subclass with a computing
        deref runs that deref once and refs the returned object
        """

        class Box(object):
            pass
        box = Box()
        box.foo = "via-ref"
        calls = []

        class Computing(Ref):
            def deref(self):
                calls.append(1)
                return box

        code, ns = build_expr([
            ["FAST_ATTR_REF", (0, "foo", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        ns.bind("obj", Computing("obj"))
        ref = await make_callable(code, ns)()
        self.assertEqual(ref.deref(), "via-ref")
        self.assertEqual(len(calls), 1)


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


    async def test_fast_attr_slots_load_repeats_through_cache(self):
        """
        FAST_ATTR on a __slots__ member answers the same on the first
        (populating) and second (cached) execution
        """

        class Box(object):
            __slots__ = ("value",)
        box = Box()
        box.value = "slotted"
        code, ns = build_expr([
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), values={"obj": box})
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), "slotted")
        self.assertEqual(await fn(), "slotted")


    async def test_fast_attr_store_slots_repeats_and_keeps_refcount(self):
        """
        FAST_ATTR_STORE into a __slots__ member twice leaves the value
        with exactly the member's reference
        """

        import sys

        class Box(object):
            __slots__ = ("value",)
        box = Box()
        payload = object()
        before = sys.getrefcount(payload)
        code, ns = build_expr([
            ["CONST", payload],
            ["FAST_ATTR_STORE", (0, "value", 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], local_syms=("obj",), values={"obj": box})
        fn = make_callable(code, ns)
        await fn()
        await fn()
        self.assertIs(box.value, payload)
        del box.value
        del code, ns, fn
        self.assertEqual(sys.getrefcount(payload), before)


    async def test_fast_attr_property_takes_generic_path(self):
        """
        FAST_ATTR on a property computes through getattr each time
        """

        class Box(object):
            count = 0

            @property
            def value(self):
                Box.count += 1
                return Box.count
        box = Box()
        code, ns = build_expr([
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), values={"obj": box})
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), 1)
        self.assertEqual(await fn(), 2)


    async def test_fast_attr_unset_slot_falls_back_to_getattr(self):
        """
        FAST_ATTR on an unset __slots__ member runs __getattr__
        """

        class Box(object):
            __slots__ = ("value",)

            def __getattr__(self, name):
                return "fallback:" + name
        box = Box()
        code, ns = build_expr([
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",), values={"obj": box})
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), "fallback:value")
        box.value = "set"
        self.assertEqual(await fn(), "set")


    async def test_fast_attr_store_setattr_override_takes_generic_path(self):
        """
        FAST_ATTR_STORE on a type with __setattr__ goes through it
        """

        seen = []

        class Box(object):
            def __setattr__(self, name, value):
                seen.append((name, value))
        box = Box()
        code, ns = build_expr([
            ["CONST", 5],
            ["FAST_ATTR_STORE", (0, "value", 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], local_syms=("obj",), values={"obj": box})
        fn = make_callable(code, ns)
        await fn()
        await fn()
        self.assertEqual(seen, [("value", 5), ("value", 5)])


    async def test_fast_attr_unbound_slot_raises(self):
        """
        FAST_ATTR and FAST_ATTR_STORE on an unbound fast slot raise
        NameError
        """

        code, ns = build_expr([
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()

        code, ns = build_expr([
            ["CONST", 5],
            ["FAST_ATTR_STORE", (0, "value", 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], local_syms=("obj",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_fast_attr_computing_ref_derefs_once(self):
        """
        FAST_ATTR on a slot bound to a Ref subclass with a computing
        deref runs that deref once per execution
        """

        class Box(object):
            pass
        box = Box()
        box.value = "via-ref"
        calls = []

        class Computing(Ref):
            def deref(self):
                calls.append(1)
                return box

        code, ns = build_expr([
            ["FAST_ATTR", (0, "value", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        ns.bind("obj", Computing("obj"))
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), "via-ref")
        self.assertEqual(await fn(), "via-ref")
        self.assertEqual(len(calls), 2)


# The end.
