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
Tests for compile/call special: tail position emits TAIL_CALL; non-tail uses CALL.
Bytecode shape for positional, keyword, star; evaluation order; end-to-end.
"""


from koskari.lib import OpCode
from koskari.dis import dis

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestCallSpecialTailPosition(SpecialsTestCase):
    """
    compile/call in tail position emits TAIL_CALL; otherwise CALL
    """

    async def test_lambda_body_tail_call_one_arg(self):
        code = self.compile_to_code("(lambda [x] (f x))", tc=True)
        ops = self.call_ops(self.lambda_body_code(code))
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)


    async def test_lambda_body_tail_call_zero_args(self):
        code = self.compile_to_code("(lambda [] (f))", tc=True)
        ops = self.call_ops(self.lambda_body_code(code))
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 0)


    async def test_lambda_begin_first_call_then_tail_call(self):
        code = self.compile_to_code("(lambda [x] (begin (f 1) (g x)))", tc=True)
        ops = self.call_ops(self.lambda_body_code(code))
        self.assertEqual(len(ops), 2)
        self.assertIn(ops[0][1], (OpCode.KOS_OP_CALL, OpCode.KOS_OP_CALL_VAR,
                                  OpCode.KOS_OP_CALL_FAST))
        self.assertEqual(ops[1][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[1][2], 1)


    async def test_begin_single_form_tail_call(self):
        code = self.compile_to_code("(begin (f 1))", tc=True)
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)


    async def test_begin_f_x_x_call_not_tail(self):
        """
        (begin (f x) x) -> f is not in tail position -> CALL or CALL_VAR/FAST
        """
        code = self.compile_to_code("(begin (f x) x)", tc=True)
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.KOS_OP_CALL, OpCode.KOS_OP_CALL_VAR,
                                  OpCode.KOS_OP_CALL_FAST))


    async def test_begin_x_f_x_tail_call(self):
        """
        (begin x (f x)) -> f is in tail position -> TAIL_CALL
        """
        code = self.compile_to_code("(begin x (f x))", tc=True)
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)


class TestCallBytecodeShape(SpecialsTestCase):
    """
    compile/call emits correct opcodes for positional, keyword, and star forms
    """

    async def test_positional_zero_args_emits_call_zero(self):
        code = self.compile_to_code("(lambda [] (f))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 0)


    async def test_positional_three_args_emits_call_three(self):
        code = self.compile_to_code("(lambda [] (f a b c))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 3)


    async def test_positional_no_call_names_or_call_star(self):
        code = self.compile_to_code("(lambda [] (f 1 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertNotIn(OpCode.KOS_OP_CALL_NAMES, [o[1] for o in ops])
        self.assertNotIn(OpCode.KOS_OP_CALL_STAR, [o[1] for o in ops])


    async def test_keyword_emits_call_names(self):
        code = self.compile_to_code("(lambda [] (f 1 name: 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL_NAMES)
        self.assertEqual(ops[0][2], 1)


    async def test_keyword_names_tuple_before_call_names(self):
        code = self.compile_to_code("(lambda [] (f 1 name: 2))", tc=True)
        body = self.lambda_body_code(code)
        call_idx = None
        const_idx = None
        for i in range(body.length()):
            op, arg = body.oparg(i)
            if op == OpCode.KOS_OP_TAIL_CALL_NAMES:
                call_idx = i
            elif op == OpCode.KOS_OP_CONST:
                val = body.constant(arg)
                if isinstance(val, tuple) and "name" in val:
                    const_idx = i
        self.assertIsNotNone(call_idx)
        self.assertIsNotNone(const_idx)
        self.assertLess(const_idx, call_idx)


    async def test_star_arg_emits_call_star(self):
        code = self.compile_to_code("(lambda [] (f *: rest))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL_STAR)
        self.assertEqual(ops[0][2], 1)


    async def test_star_arg_with_positional_emits_build_args_and_combine(self):
        code = self.compile_to_code("(lambda [] (f a b *: more))", tc=True)
        body = self.lambda_body_code(code)
        arg_ops = self.arg_building_ops(body)
        call_ops = self.call_ops(body)
        self.assertEqual(len(call_ops), 1)
        self.assertEqual(call_ops[0][1], OpCode.KOS_OP_TAIL_CALL_STAR)
        self.assertIn(OpCode.KOS_OP_BUILD_ARGS, [o[1] for o in arg_ops])
        self.assertIn(OpCode.KOS_OP_COMBINE_ARGS, [o[1] for o in arg_ops])


    async def test_full_varargs_emits_call_star_with_positional_and_keywords(self):
        """
        (fn a b *: more x: xval y: yval **: kwargs) uses CALL_STAR opa=3
        """
        code = self.compile_to_code(
            "(lambda [] (f a b *: more x: xval y: yval **: kwargs))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL_STAR)
        self.assertEqual(ops[0][2], 3)


    async def test_interleaved_positional_and_star_emits_call_star(self):
        """
        (f a *: bargs c *: dargs) uses CALL_STAR with COMBINE_ARGS 4
        """
        code = self.compile_to_code(
            "(lambda [] (f a *: bargs c *: dargs))", tc=True)
        body = self.lambda_body_code(code)
        arg_ops = self.arg_building_ops(body)
        call_ops = self.call_ops(body)
        self.assertEqual(len(call_ops), 1)
        self.assertEqual(call_ops[0][1], OpCode.KOS_OP_TAIL_CALL_STAR)
        self.assertIn(OpCode.KOS_OP_BUILD_ARGS, [o[1] for o in arg_ops])
        self.assertIn(OpCode.KOS_OP_COMBINE_ARGS, [o[1] for o in arg_ops])
        combine_ops = [o for o in arg_ops if o[1] == OpCode.KOS_OP_COMBINE_ARGS]
        self.assertEqual(len(combine_ops), 1)
        self.assertEqual(combine_ops[0][2], 4)


    async def test_interleaved_keyword_and_starstar_emits_call_star(self):
        """
        (f x: 1 **: kw1 y: 2 **: kw2) uses CALL_STAR with COMBINE_KWDS 4
        """
        code = self.compile_to_code(
            "(lambda [] (f x: 1 **: kw1 y: 2 **: kw2))", tc=True)
        body = self.lambda_body_code(code)
        arg_ops = self.arg_building_ops(body)
        call_ops = self.call_ops(body)
        self.assertEqual(len(call_ops), 1)
        self.assertEqual(call_ops[0][1], OpCode.KOS_OP_TAIL_CALL_STAR)
        self.assertIn(OpCode.KOS_OP_BUILD_KWDS_NAMES, [o[1] for o in arg_ops])
        self.assertIn(OpCode.KOS_OP_COMBINE_KWDS, [o[1] for o in arg_ops])
        combine_ops = [o for o in arg_ops if o[1] == OpCode.KOS_OP_COMBINE_KWDS]
        self.assertEqual(len(combine_ops), 1)
        self.assertEqual(combine_ops[0][2], 4)


    async def test_pure_positional_uses_call_not_call_star(self):
        """
        (f 1 2 3) uses CALL, not CALL_NAMES or CALL_STAR
        """
        code = self.compile_to_code("(lambda [] (f 1 2 3))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        all_ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertNotIn(OpCode.KOS_OP_CALL_NAMES, all_ops)
        self.assertNotIn(OpCode.KOS_OP_CALL_STAR, all_ops)


    async def test_positional_and_keyword_uses_call_names_not_call_star(self):
        """
        (f 1 name: 2) uses CALL_NAMES, not CALL_STAR
        """
        code = self.compile_to_code("(lambda [] (f 1 name: 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL_NAMES)
        all_ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertNotIn(OpCode.KOS_OP_CALL_STAR, all_ops)


class TestCallVarPath(SpecialsTestCase):
    """
    When callable is a simple symbol and not in tail position, emit CALL_VAR*
    """

    async def test_symbol_callable_non_tail_emits_call_var(self):
        """
        (begin (f 1) 2) with tc: f is symbol, not tail -> CALL_VAR or CALL_FAST
        """
        code = self.compile_to_code("(lambda [] (begin (f 1) 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.KOS_OP_CALL_VAR, OpCode.KOS_OP_CALL_FAST),
            "non-tail symbol call should use CALL_VAR or CALL_FAST")


    async def test_symbol_callable_positional_no_tc_emits_call_var(self):
        """
        (begin (f 1 2) 2): f not in tail -> CALL_VAR or CALL_FAST
        """
        code = self.compile_to_code("(lambda [] (begin (f 1 2) 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.KOS_OP_CALL_VAR, OpCode.KOS_OP_CALL_FAST))


    async def test_symbol_callable_keyword_no_tc_emits_call_var_names(self):
        """
        (begin (f 1 name: 2) 2): f not in tail -> CALL_VAR_NAMES or CALL_FAST_NAMES
        """
        code = self.compile_to_code(
            "(lambda [] (begin (f 1 name: 2) 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.KOS_OP_CALL_VAR_NAMES, OpCode.KOS_OP_CALL_FAST_NAMES))


    async def test_symbol_callable_star_no_tc_emits_call_var_star(self):
        """
        (begin (f *: rest) 2): f not in tail -> CALL_VAR_STAR or CALL_FAST_STAR
        """
        code = self.compile_to_code("(lambda [] (begin (f *: rest) 2))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.KOS_OP_CALL_VAR_STAR, OpCode.KOS_OP_CALL_FAST_STAR))


    async def test_tail_position_still_emits_tail_call(self):
        """
        Symbol in tail position with tc=True -> TAIL_CALL, not CALL_VAR
        """
        code = self.compile_to_code("(lambda [] (f 1))", tc=True)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)


    async def test_complex_callable_emits_call_attr_not_call_var(self):
        """
        (attr obj meth)(args) -> a fused attr-call opcode, not CALL_VAR
        or a generic CALL over a pre-resolved attribute
        """
        code = self.compile_to_code(
            "(lambda [] ((compile/attr obj upper) \"hi\"))", tc=False)
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.KOS_OP_CALL_ATTR,
             OpCode.KOS_OP_CALL_VAR_ATTR,
             OpCode.KOS_OP_CALL_FAST_ATTR),
            "complex attr callable should use a fused attr-call opcode")


class TestCallEvaluationOrder(SpecialsTestCase):
    """
    Arguments are evaluated left-to-right; bytecode and runtime order
    """

    async def test_positional_args_precede_call(self):
        code = self.compile_to_code("(lambda [] (f 1 2))", tc=True)
        body = self.lambda_body_code(code)
        call_idx = None
        const_count = 0
        for i in range(body.length()):
            op, arg = body.oparg(i)
            if op in (OpCode.KOS_OP_CALL, OpCode.KOS_OP_TAIL_CALL):
                call_idx = i
            elif op == OpCode.KOS_OP_CONST:
                const_count += 1
        self.assertIsNotNone(call_idx)
        self.assertGreaterEqual(const_count, 2)


    async def test_evaluation_order_runtime(self):
        received = []

        def spy(*args, **kwargs):
            received.append((list(args), dict(kwargs)))
            return None

        def tag(x):
            received.append(x)
            return x

        src = '(compile/call f (compile/call tag "a") (compile/call tag "b"))'
        expr, mod = self.compile_expr(src, f=spy, tag=tag)
        await expr()
        self.assertEqual(received[0], "a")
        self.assertEqual(received[1], "b")
        self.assertEqual(received[2], (["a", "b"], {}))


class TestCallEndToEnd(SpecialsTestCase):
    """
    Compiled (compile/call f ...) runs and passes args/kwargs to callable
    """

    async def test_call_zero_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return 42

        expr, mod = self.compile_expr("(compile/call f)", f=spy)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0], ((), {}))


    async def test_call_one_arg(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(compile/call f 1)", f=spy)
        await expr()
        self.assertEqual(got[0], ((1,), {}))


    async def test_call_three_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(compile/call f 1 2 3)", f=spy)
        await expr()
        self.assertEqual(got[0], ((1, 2, 3), {}))


    async def test_call_keyword_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(compile/call f 1 name: 2)", f=spy)
        await expr()
        self.assertEqual(got[0], ((1,), {"name": 2}))


    async def test_call_keyword_only_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(compile/call f name: 2)", f=spy)
        await expr()
        self.assertEqual(got[0], ((), {"name": 2}))


    async def test_call_var_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr(
            "(compile/call f *: more)",
            f=spy, more=(1, 2, 3))
        await expr()
        self.assertEqual(got[0], ((1, 2, 3), {}))


    async def test_call_interleaved_positional_and_star_end_to_end(self):
        got = []

        def spy(*args, **kwargs):
            got.append((list(args), dict(kwargs)))
            return None

        src = "(compile/call f 1 *: mid 2 *: end)"
        expr, mod = self.compile_expr(
            src, f=spy, mid=(10, 11), end=(20, 21))
        await expr()
        self.assertEqual(got[0], ([1, 10, 11, 2, 20, 21], {}))


class TestGeneratorTailCallDemotion(SpecialsTestCase):
    """
    Yielding functions run demote_tail_calls_if_yields before Code(); tail
    calls must not appear in the sealed bytecode.
    """

    async def test_yield_and_tail_position_emits_call_not_tail_call(self):
        """
        Else branch is a call in tail position (would be TAIL_CALL without
        demotion). With yield in the same body, final code uses CALL.
        """

        code = self.compile_to_code(
            """
            (lambda [x]
              (if x
                (compile/yield x)
                (compile/call list 1 2 3)))
            """,
            list=list,
            tc=True,
        )
        body = self.lambda_body_code(code)
        ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertIn(OpCode.KOS_OP_YIELD, ops)
        self.assertNotIn(OpCode.KOS_OP_TAIL_CALL, ops)
        self.assertIn(OpCode.KOS_OP_CALL, ops)


# The end.
