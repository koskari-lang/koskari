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
Tests for call-attr opcodes: CALL_ATTR (obj on stack); CALL_FAST_ATTR /
CALL_FAST_ATTR_NAMES (obj from fast slot); CALL_VAR_ATTR / CALL_VAR_ATTR_NAMES
(obj from VAR by name). EXTRA shapes and stack layouts match each opcode's
schema; resolves obj.attr and invokes with (obj, *args) for unbound fn, else
(*args).
"""

from koskari.types import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestCallAttr(CevalTest):

    async def test_call_attr_zero_args_str_upper(self):
        """
        CALL_ATTR ("upper", 0): "hello".upper() -> "HELLO"
        Stack: [obj="hello"]
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["CALL_ATTR", ("upper", 0, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


    async def test_call_attr_one_arg_str_split(self):
        """
        CALL_ATTR ("split", 1): "a-b-c".split("-") -> ["a", "b", "c"]
        Stack: [obj, arg_0]
        """

        code, ns = build_expr([
            ["CONST", "a-b-c"],
            ["CONST", "-"],
            ["CALL_ATTR", ("split", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, ["a", "b", "c"])


    async def test_call_attr_two_args_str_replace(self):
        """
        CALL_ATTR ("replace", 2): "hello".replace("l", "L") -> "heLLo"
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["CONST", "l"],
            ["CONST", "L"],
            ["CALL_ATTR", ("replace", 2, 0, 0)],
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
            ["CONST", 42],
            ["CALL_ATTR", ("nonexistent", 0, 0, 0)],
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
            ["CONST", 1],
            ["CALL_ATTR", ("numerator", 0, 0, 0)],
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
            ["CONST", c],
            ["CONST", 41],
            ["CALL_ATTR", ("add_one", 1, 0, 0)],
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
            ["CONST", c],
            ["CALL_ATTR", ("m", 0, 0, 0)],
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
            ["CONST", c],
            ["CONST", 21],
            ["CALL_ATTR", ("dynamic", 1, 0, 0)],
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
            ["CONST", c],
            ["CALL_ATTR", ("getter", 0, 0, 0)],
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
            ["CONST", c],
            ["CALL_ATTR", ("custom", 0, 0, 0)],
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
            ["CONST", c],
            ["CONST", 41],
            ["CALL_ATTR", ("f", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


class TestCallFastAttrNames(CevalTest):
    """
    CALL_FAST_ATTR_NAMES: obj from fast slot, stack [pos..., kw...]
    """

    async def test_call_fast_attr_names_zero_args(self):
        """
        obj.upper() with obj in slot 0
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["FAST_STORE", 0],
            ["CALL_FAST_ATTR_NAMES", (0, "upper", (), 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


    async def test_call_fast_attr_names_one_pos(self):
        """
        obj.split(sep) with obj in slot 0
        """

        code, ns = build_expr([
            ["CONST", "a-b-c"],
            ["FAST_STORE", 0],
            ["CONST", "-"],
            ["CALL_FAST_ATTR_NAMES", (0, "split", (), 1)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, ["a", "b", "c"])


    async def test_call_fast_attr_names_with_kw(self):
        """
        obj.m(1, name: 2) with obj in slot 0
        """

        received = []

        def recorder(self, x, name=0):
            received.append((x, name))
            return None

        obj = type('Obj', (), {'m': recorder})()
        code, ns = build_expr([
            ["CONST", obj],
            ["FAST_STORE", 0],
            ["CONST", 99],
            ["CONST", 42],
            ["CALL_FAST_ATTR_NAMES", (0, "m", ("name",), 1)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        await fn()
        self.assertEqual(received, [(99, 42)])


    async def test_call_fast_attr_names_unbound_method(self):
        """
        Unbound Python method path (send_self=0)
        """

        class C:
            def add_one(self, x):
                return x + 1

        c = C()
        code, ns = build_expr([
            ["CONST", c],
            ["FAST_STORE", 0],
            ["CONST", 41],
            ["CALL_FAST_ATTR_NAMES", (0, "add_one", (), 1)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


class TestCallFastAttr(CevalTest):
    """
    CALL_FAST_ATTR: obj from fast slot, stack [arg_0, ..., arg_{N-1}] only.
    """

    async def test_call_fast_attr_zero_args(self):
        """
        obj.upper() with obj in slot 0
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["FAST_STORE", 0],
            ["CALL_FAST_ATTR", (0, "upper", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


    async def test_call_fast_attr_one_pos(self):
        """
        obj.split(sep) with obj in slot 0
        """

        code, ns = build_expr([
            ["CONST", "a-b-c"],
            ["FAST_STORE", 0],
            ["CONST", "-"],
            ["CALL_FAST_ATTR", (0, "split", 1, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, ["a", "b", "c"])


    async def test_call_fast_attr_unbound_method(self):
        """
        Unbound Python method path (send_self=0)
        """

        class C:
            def add_one(self, x):
                return x + 1

        c = C()
        code, ns = build_expr([
            ["CONST", c],
            ["FAST_STORE", 0],
            ["CONST", 41],
            ["CALL_FAST_ATTR", (0, "add_one", 1, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


    async def test_call_fast_attr_missing_attr_raises(self):
        """
        CALL_FAST_ATTR on object without attribute raises AttributeError
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["FAST_STORE", 0],
            ["CALL_FAST_ATTR", (0, "nonexistent", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        fn = make_callable(code, ns)
        with self.assertRaises(AttributeError) as ctx:
            await fn()
        self.assertIn("int", str(ctx.exception))
        self.assertIn("nonexistent", str(ctx.exception))


class TestCallVarAttrNames(CevalTest):
    """
    CALL_VAR_ATTR_NAMES: obj from VAR (name lookup), stack [pos..., kw...]
    Uses a Function with formals so the namespace has the obj binding.
    """

    async def test_call_var_attr_names_zero_args(self):
        """
        obj.upper() with obj looked up by name (function param)
        """

        code = build_code([
            ["CALL_VAR_ATTR_NAMES", ("obj", "upper", (), 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        result = await fn("hello")
        self.assertEqual(result, "HELLO")


    async def test_call_var_attr_names_one_pos(self):
        """
        obj.split(sep) with obj by name
        """

        code = build_code([
            ["CONST", "-"],
            ["CALL_VAR_ATTR_NAMES", ("obj", "split", (), 1)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        result = await fn("a-b-c")
        self.assertEqual(result, ["a", "b", "c"])


    async def test_call_var_attr_names_with_kw(self):
        """
        obj.m(1, name: 2) with obj by name
        """

        received = []

        def recorder(self, x, name=0):
            received.append((x, name))
            return None

        obj = type('Obj', (), {'m': recorder})()
        code = build_code([
            ["CONST", 99],
            ["CONST", 42],
            ["CALL_VAR_ATTR_NAMES", ("obj", "m", ("name",), 1)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        await fn(obj)
        self.assertEqual(received, [(99, 42)])


class TestCallVarAttr(CevalTest):
    """
    CALL_VAR_ATTR: obj from VAR (name lookup), stack [arg_0, ..., arg_{N-1}] only.
    Uses a Function with formals so the namespace has the obj binding.
    """

    async def test_call_var_attr_zero_args(self):
        """
        obj.upper() with obj looked up by name (function param)
        """

        code = build_code([
            ["CALL_VAR_ATTR", ("obj", "upper", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        result = await fn("hello")
        self.assertEqual(result, "HELLO")


    async def test_call_var_attr_one_pos(self):
        """
        obj.split(sep) with obj by name
        """

        code = build_code([
            ["CONST", "-"],
            ["CALL_VAR_ATTR", ("obj", "split", 1, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        result = await fn("a-b-c")
        self.assertEqual(result, ["a", "b", "c"])


    async def test_call_var_attr_unbound_method(self):
        """
        Unbound Python method path (send_self=0)
        """

        class C:
            def add_one(self, x):
                return x + 1

        c = C()
        code = build_code([
            ["CONST", 41],
            ["CALL_VAR_ATTR", ("obj", "add_one", 1, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        result = await fn(c)
        self.assertEqual(result, 42)


    async def test_call_var_attr_missing_attr_raises(self):
        """
        CALL_VAR_ATTR on object without attribute raises AttributeError
        """

        code = build_code([
            ["CALL_VAR_ATTR", ("obj", "nonexistent", 0, 0)],
            ["RETURN", 1],
        ], local_syms=("obj",))
        nspec = NamespaceSpec(None, ("obj",), ())
        ns = nspec.get_namespace(None)
        fn = make_callable(code, ns, FormalsSpec(("obj",)))
        with self.assertRaises(AttributeError) as ctx:
            await fn(42)
        self.assertIn("int", str(ctx.exception))
        self.assertIn("nonexistent", str(ctx.exception))


# The end.
