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
Tests for compile/call-attr special form. Emits CALL_ATTR for positional-only,
CALL_ATTR_NAMES for keyword args, CALL_ATTR_STAR for *args/**kwargs when the
receiver is compiled as an expression. When the receiver is a bare symbol that
is a fast local (e.g. lambda formal), the compiler emits CALL_VAR_ATTR* first,
then convert_fast lowers matching opcodes to CALL_FAST_ATTR* in the final Code.
"""


from koskari.lib import OpCode
from koskari.exceptions import SpecialError, SyntaxMatchError

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


CALL_ATTR_OPCODES = (
    OpCode.KOS_OP_CALL_ATTR,
    OpCode.KOS_OP_CALL_ATTR_NAMES,
    OpCode.KOS_OP_CALL_ATTR_STAR,
)

CALL_ATTR_OR_FAST_OPCODES = CALL_ATTR_OPCODES + (
    OpCode.KOS_OP_CALL_VAR_ATTR,
    OpCode.KOS_OP_CALL_VAR_ATTR_NAMES,
    OpCode.KOS_OP_CALL_VAR_ATTR_STAR,
    OpCode.KOS_OP_CALL_FAST_ATTR,
    OpCode.KOS_OP_CALL_FAST_ATTR_NAMES,
    OpCode.KOS_OP_CALL_FAST_ATTR_STAR,
)


def call_attr_ops(code):
    """
    List of (index, opcode, arg) for CALL_ATTR* opcodes in code
    """

    return [(i, op, arg) for i in range(code.length())
            for op, arg in [code.oparg(i)]
            if op in CALL_ATTR_OPCODES]


def call_attr_family_ops(code):
    """
    List of (index, opcode, arg) for CALL_ATTR / CALL_VAR_ATTR / CALL_FAST_ATTR
    family opcodes in code
    """

    return [(i, op, arg) for i in range(code.length())
            for op, arg in [code.oparg(i)]
            if op in CALL_ATTR_OR_FAST_OPCODES]


class TestCallAttrBytecodeShape(SpecialsTestCase):
    """
    compile/call-attr emits correct opcodes for positional, keyword, star
    """

    async def test_positional_zero_args_emits_call_attr(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr s upper))", s="hello")
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(body.constant(a0), "upper")
        self.assertEqual(a1, 0)


    async def test_positional_one_arg_emits_call_attr(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr s split sep))",
            s="a-b-c", sep="-")
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(body.constant(a0), "split")
        self.assertEqual(a1, 1)


    async def test_positional_three_args_emits_call_attr(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr s replace a b))",
            s="hello", a="l", b="L")
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(body.constant(a0), "replace")
        self.assertEqual(a1, 2)


    async def test_keyword_emits_call_attr_names(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr obj m 1 name: 2))",
            obj=object(), m=lambda self, x, name=0: (x, name))
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR_NAMES)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(body.constant(a0), "m")
        self.assertEqual(body.constant(a1), ("name",))
        self.assertEqual(a2, 1)


    async def test_star_arg_emits_call_attr_star(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr obj m *: rest))",
            obj=object(), m=lambda *a: a, rest=(1, 2, 3))
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR_STAR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(a1, 1)


    async def test_starstar_arg_emits_call_attr_star(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr obj m **: kw))",
            obj=object(), m=lambda **kw: kw, kw={'x': 1})
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR_STAR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(a1, 2)


    async def test_positional_uses_call_attr_not_call_attr_names(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call-attr s upper))", s="x")
        body = self.lambda_body_code(code)
        all_ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertIn(OpCode.KOS_OP_CALL_ATTR, all_ops)
        self.assertNotIn(OpCode.KOS_OP_CALL_ATTR_NAMES, all_ops)
        self.assertNotIn(OpCode.KOS_OP_CALL_ATTR_STAR, all_ops)


class TestCallAttrNameMustBeSymbol(SpecialsTestCase):

    async def test_non_symbol_name_raises_syntax_match_error(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_expr("(compile/call-attr obj 42)", obj="hello")


class TestCallAttrEndToEnd(SpecialsTestCase):
    """
    Compiled (compile/call-attr obj name args...) runs and invokes method
    """

    async def test_zero_args_str_upper(self):
        s = "hello"
        expr, mod = self.compile_expr("(compile/call-attr s upper)", s=s)
        result = await expr()
        self.assertEqual(result, "HELLO")


    async def test_one_arg_str_split(self):
        s = "a-b-c"
        sep = "-"
        expr, mod = self.compile_expr(
            "(compile/call-attr s split sep)", s=s, sep=sep)
        result = await expr()
        self.assertEqual(result, ["a", "b", "c"])


    async def test_two_args_str_replace(self):
        s = "hello"
        expr, mod = self.compile_expr(
            "(compile/call-attr s replace \"l\" \"L\")", s=s)
        result = await expr()
        self.assertEqual(result, "heLLo")


    async def test_keyword_args(self):
        received = []

        def recorder(self, x, name=0):
            received.append((x, name))
            return None

        obj = type('Obj', (), {'m': recorder})()
        expr, mod = self.compile_expr(
            "(compile/call-attr obj m 99 name: 42)", obj=obj)
        await expr()
        self.assertEqual(received, [(99, 42)])


    async def test_star_args_str_join(self):
        sep = ", "
        rest = (["a", "b", "c"],)
        expr, mod = self.compile_expr(
            "(compile/call-attr sep join *: rest)",
            sep=sep, rest=rest)
        result = await expr()
        self.assertEqual(result, "a, b, c")


    async def test_starstar_kwargs(self):
        def m(self, **kw):
            return kw

        obj = type('Obj', (), {'m': m})()
        kwargs = {'x': 1, 'y': 2}
        expr, mod = self.compile_expr(
            "(compile/call-attr obj m **: kwargs)",
            obj=obj, kwargs=kwargs)
        result = await expr()
        self.assertEqual(result, {'x': 1, 'y': 2})


class TestCallAttrExpansion(SpecialsTestCase):
    """
    (compile/call (attr obj name) args) and (compile/call (compile/attr obj name) args)
    expand to (compile/call-attr obj name args) and emit CALL_ATTR
    """

    async def test_compile_call_compile_attr_emits_call_attr(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call (compile/attr s upper)))", s="hello")
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(body.constant(a0), "upper")
        self.assertEqual(a1, 0)


    async def test_compile_call_attr_emits_call_attr(self):
        code = self.compile_to_code(
            "(lambda [] (compile/call (attr obj m) 1 2))",
            obj=object(), m=lambda self, a, b: (a, b))
        body = self.lambda_body_code(code)
        ops = call_attr_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(body.constant(a0), "m")
        self.assertEqual(a1, 2)


    async def test_compile_call_compile_attr_end_to_end(self):
        s = "hello"
        expr, mod = self.compile_expr(
            "(compile/call (compile/attr s upper))", s=s)
        result = await expr()
        self.assertEqual(result, "HELLO")


    async def test_compile_call_attr_end_to_end(self):
        obj = type('Obj', (), {'m': lambda self, a, b: a + b})()
        expr, mod = self.compile_expr(
            "(compile/call (attr obj m) 10 32)", obj=obj)
        result = await expr()
        self.assertEqual(result, 42)


class TestCallAttrLocalReceiverLowersToFast(SpecialsTestCase):
    """
    Lambda formals are fast locals; attribute calls on a formal symbol should
    appear as CALL_FAST_ATTR* after compile (VAR path + convert_fast).

    The call form must not be the lambda body's tail expression: the body is
    compiled with tail-call tracking, which forces the stack path for
    compile/call-attr. Wrap in begin with a trailing value so the attr call is
    compiled with tc=False and the VAR path (then convert_fast) applies.
    """

    async def test_local_receiver_positional_emits_call_fast_attr(self):
        code = self.compile_to_code(
            "(lambda [obj] (begin (compile/call-attr obj upper) 0))")
        body = self.lambda_body_code(code)
        ops = call_attr_family_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_FAST_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(a0, 0)
        self.assertEqual(body.constant(a1), "upper")
        self.assertEqual(a2, 0)


    async def test_local_receiver_via_compile_call_attr_emits_call_fast_attr(self):
        code = self.compile_to_code(
            "(lambda [obj] (begin (compile/call (attr obj m) 1 2) 0))",
            m=lambda self, a, b: (a, b))
        body = self.lambda_body_code(code)
        ops = call_attr_family_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_FAST_ATTR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(a0, 0)
        self.assertEqual(body.constant(a1), "m")
        self.assertEqual(a2, 2)


    async def test_local_receiver_keyword_emits_call_fast_attr_names(self):
        code = self.compile_to_code(
            "(lambda [obj] (begin (compile/call-attr obj m 1 name: 2) 0))",
            m=lambda self, x, name=0: (x, name))
        body = self.lambda_body_code(code)
        ops = call_attr_family_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_FAST_ATTR_NAMES)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(a0, 0)
        self.assertEqual(body.constant(a1), "m")
        self.assertEqual(body.constant(a2), ("name",))
        self.assertEqual(a3, 1)


    async def test_local_receiver_starstar_emits_call_fast_attr_star(self):
        code = self.compile_to_code(
            "(lambda [obj] (begin (compile/call-attr obj m **: kw) 0))",
            m=lambda self, **kw: kw,
            kw={'x': 1})
        body = self.lambda_body_code(code)
        ops = call_attr_family_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL_FAST_ATTR_STAR)
        extra_idx = ops[0][2]
        a0, a1, a2, a3 = body.extra_tuple(extra_idx)
        self.assertEqual(a0, 0)
        self.assertEqual(body.constant(a1), "m")
        self.assertEqual(a2, 2)


# The end.
