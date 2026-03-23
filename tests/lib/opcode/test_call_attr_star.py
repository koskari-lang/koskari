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
Tests for CALL_ATTR_STAR opcode. EXTRA schema CONST (attr name),
COUNT (opa bitflags: 1=*args, 2=**kwargs). Stack [obj, pos, kwd] or
subsets. Resolves obj.name, prepends obj when send_self, calls via
KosContext_CallArgs.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestCallAttrStarBasic(CevalTest):

    async def test_opa_zero_obj_method_no_args(self):
        """
        CALL_ATTR_STAR opa=0: obj.method() with no *args/**kwargs
        Stack: [obj]. pos=(), kwd=NULL.
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["CALL_ATTR_STAR", ("upper", 0, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


    async def test_opa_one_star_args_str_split(self):
        """
        CALL_ATTR_STAR opa=1: obj.split(*("-",)) -> ["a","b","c"]
        Stack: [obj, pos]
        """

        code, ns = build_expr([
            ["CONST", "a-b-c"],
            ["CONST", "-"],
            ["BUILD_ARGS", 1],
            ["CALL_ATTR_STAR", ("split", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, ["a", "b", "c"])


    async def test_opa_one_empty_args_str_upper(self):
        """
        CALL_ATTR_STAR opa=1: "hello".upper(*()) -> "HELLO"
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["BUILD_ARGS", 0],
            ["CALL_ATTR_STAR", ("upper", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


    async def test_opa_two_starstar_kwargs(self):
        """
        CALL_ATTR_STAR opa=2: obj.m(**{"x": 1}) with m(self, x=0) -> 1
        """

        received = []

        def recorder(self, x=0):
            received.append(x)
            return x

        obj = type("Obj", (), {"m": recorder})()
        code, ns = build_expr([
            ["CONST", obj],
            ["CONST", "x"],
            ["CONST", 1],
            ["BUILD_KWDS", 1],
            ["CALL_ATTR_STAR", ("m", 2, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 1)
        self.assertEqual(received, [1])


    async def test_opa_three_star_and_starstar(self):
        """
        CALL_ATTR_STAR opa=3: obj.m(*args, **kwargs) combined
        """

        def adder(self, a, b, c=0):
            return a + b + c

        obj = type("Obj", (), {"m": adder})()
        code, ns = build_expr([
            ["CONST", obj],
            ["CONST", 10],
            ["CONST", 20],
            ["BUILD_ARGS", 2],
            ["CONST", "c"],
            ["CONST", 12],
            ["BUILD_KWDS", 1],
            ["CALL_ATTR_STAR", ("m", 3, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


class TestCallAttrStarSendSelf(CevalTest):

    async def test_unbound_method_prepends_self(self):
        """
        Unbound Python method: send_self=0, obj prepended to args
        """

        class C:
            def add_one(self, x):
                return x + 1

        c = C()
        code, ns = build_expr([
            ["CONST", c],
            ["CONST", 41],
            ["BUILD_ARGS", 1],
            ["CALL_ATTR_STAR", ("add_one", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, 42)


    async def test_bound_method_omits_self(self):
        """
        Bound method (str.upper): send_self=1, obj not in args
        """

        code, ns = build_expr([
            ["CONST", "hello"],
            ["BUILD_ARGS", 0],
            ["CALL_ATTR_STAR", ("upper", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "HELLO")


class TestCallAttrStarErrors(CevalTest):

    async def test_missing_attr_raises_attribute_error(self):
        """
        CALL_ATTR_STAR on object without attribute raises AttributeError
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["CALL_ATTR_STAR", ("nonexistent", 0, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(AttributeError) as ctx:
            await fn()
        self.assertIn("int", str(ctx.exception))
        self.assertIn("nonexistent", str(ctx.exception))


    async def test_non_callable_raises_type_error(self):
        """
        CALL_ATTR_STAR when resolved attr is not callable raises TypeError
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CALL_ATTR_STAR", ("numerator", 0, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError) as ctx:
            await fn()
        self.assertIn("not callable", str(ctx.exception))


class TestCallAttrStarStrJoin(CevalTest):

    async def test_str_join_via_star_args(self):
        """
        ", ".join(*[["a","b","c"]]) -> "a, b, c"
        """

        code, ns = build_expr([
            ["CONST", ", "],
            ["CONST", ["a", "b", "c"]],
            ["BUILD_ARGS", 1],
            ["CALL_ATTR_STAR", ("join", 1, 0, 0)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, "a, b, c")


# The end.
