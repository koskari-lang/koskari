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
Tests for CALL_ATTR opcode. Stack [name, obj, arg_0, ..., arg_{N-1}].
Resolves obj.name and invokes with (obj, *args) for unbound fn, else (*args).
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestCallAttr(CevalTest):

    async def test_call_attr_zero_args_str_upper(self):
        """
        CALL_ATTR 0: "hello".upper() -> "HELLO"
        Stack: [name="upper", obj="hello"]
        """

        code, ns = build_expr([
            ["CONST", "upper"],
            ["CONST", "hello"],
            ["CALL_ATTR", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


    async def test_call_attr_one_arg_str_split(self):
        """
        CALL_ATTR 1: "a-b-c".split("-") -> ["a", "b", "c"]
        Stack: [name, obj, arg_0]
        """

        code, ns = build_expr([
            ["CONST", "split"],
            ["CONST", "a-b-c"],
            ["CONST", "-"],
            ["CALL_ATTR", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, ["a", "b", "c"])


    async def test_call_attr_two_args_str_replace(self):
        """
        CALL_ATTR 2: "hello".replace("l", "L") -> "heLLo"
        """

        code, ns = build_expr([
            ["CONST", "replace"],
            ["CONST", "hello"],
            ["CONST", "l"],
            ["CONST", "L"],
            ["CALL_ATTR", 2],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "heLLo")


    async def test_call_attr_missing_attr_raises(self):
        """
        CALL_ATTR on object without attribute raises AttributeError
        """

        code, ns = build_expr([
            ["CONST", "nonexistent"],
            ["CONST", 42],
            ["CALL_ATTR", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(AttributeError) as ctx:
            await fn()
        self.assertIn("int", str(ctx.exception))
        self.assertIn("nonexistent", str(ctx.exception))


    async def test_call_attr_non_callable_raises(self):
        """
        CALL_ATTR when resolved attr is not callable raises TypeError.
        (1).numerator is 1, an int, not callable.
        """

        code, ns = build_expr([
            ["CONST", "numerator"],
            ["CONST", 1],
            ["CALL_ATTR", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError) as ctx:
            await fn()
        self.assertIn("not callable", str(ctx.exception))
        self.assertIn("int", str(ctx.exception))


class TestCallAttrUnboundPath(CevalTest):
    """
    Unbound plain instance method path (PyFunction returned without __get__).
    Resolver returns descriptor; caller invokes fn(obj, *args).
    """

    async def test_call_attr_unbound_python_method(self):
        """
        Python class with def method; resolver returns unbound function.
        """

        class C:
            def add_one(self, x):
                return x + 1

        c = C()
        code, ns = build_expr([
            ["CONST", "add_one"],
            ["CONST", c],
            ["CONST", 41],
            ["CALL_ATTR", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


class TestCallAttrInstanceDictShadow(CevalTest):
    """
    Instance dict shadows class attribute. Resolver finds name in __dict__.
    """

    async def test_call_attr_instance_dict_callable(self):
        """
        Instance has callable in __dict__ that shadows class.
        """

        class C:
            pass

        c = C()
        c.m = lambda: 99
        code, ns = build_expr([
            ["CONST", "m"],
            ["CONST", c],
            ["CALL_ATTR", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 99)


class TestCallAttrGetattr(CevalTest):
    """
    __getattr__ fallback when attribute not found by normal lookup.
    """

    async def test_call_attr_getattr_fallback(self):
        """
        Object with __getattr__ that returns a callable.
        """

        class C:
            def __getattr__(self, name):
                if name == "dynamic":
                    return lambda x: x * 2
                raise AttributeError(name)

        c = C()
        code, ns = build_expr([
            ["CONST", "dynamic"],
            ["CONST", c],
            ["CONST", 21],
            ["CALL_ATTR", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


class TestCallAttrProperty(CevalTest):
    """
    Data descriptor (property) returning a callable.
    """

    async def test_call_attr_property_returns_callable(self):
        """
        Property returns a callable (not a plain function); bound path.
        Uses a callable instance so we do not hit is_plain_instance_method.
        """

        class CallableResult:
            def __call__(self):
                return 123

        class C:
            @property
            def getter(self):
                return CallableResult()

        c = C()
        code, ns = build_expr([
            ["CONST", "getter"],
            ["CONST", c],
            ["CALL_ATTR", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 123)


class TestCallAttrCustomGetattribute(CevalTest):
    """
    Custom __getattribute__ (tp_getattro != PyObject_GenericGetAttr).
    Resolver delegates to PyObject_GetAttr.
    """

    async def test_call_attr_custom_getattribute(self):
        """
        Class overriding __getattribute__; resolver delegates to PyObject_GetAttr.
        Uses callable instance so we hit bound path (not is_plain_instance_method).
        """

        class CallableResult:
            def __call__(self):
                return 456

        class C:
            def __getattribute__(self, name):
                if name == "custom":
                    return CallableResult()
                return object.__getattribute__(self, name)

        c = C()
        code, ns = build_expr([
            ["CONST", "custom"],
            ["CONST", c],
            ["CALL_ATTR", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 456)


    async def test_call_attr_custom_getattribute_plain_function(self):
        """
        If custom __getattribute__ returns a plain function object, CALL_ATTR
        must not inject self again.
        """

        class C:
            def __getattribute__(self, name):
                if name == "f":
                    return lambda x: x + 1
                return object.__getattribute__(self, name)

        c = C()
        code, ns = build_expr([
            ["CONST", "f"],
            ["CONST", c],
            ["CONST", 41],
            ["CALL_ATTR", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


# The end.
