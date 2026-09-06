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
Tests for call special: the processor promotes tail-position calls in
FUNCTION-mode streams onto their TAIL twins; expression streams and
non-tail sites keep the plain forms. Bytecode shape for positional,
keyword, star; evaluation order; end-to-end.
"""


from koskari.lib import OpCode
from tests.dis.dis import dis

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


TAIL_CALL_OPCODES = tuple(
    op for op in OpCode if op.name.startswith('TAIL_CALL'))


class TestCallSpecialTailPosition(SpecialsTestCase):
    """
    a call in tail position is relabeled onto its TAIL twin by the
    promotion pass; otherwise the plain call form survives
    """

    async def test_lambda_body_tail_call_one_arg(self):
        code = self.compile_to_code("(lambda [x] (f x))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        self.assertEqual(body.extra_tuple(ops[0][2])[1], 1)


    async def test_lambda_body_tail_call_zero_args(self):
        code = self.compile_to_code("(lambda [] (f))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        self.assertEqual(body.extra_tuple(ops[0][2])[1], 0)


    async def test_lambda_begin_first_call_then_tail_call(self):
        code = self.compile_to_code("(lambda [x] (begin (f 1) (g x)))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 2)
        self.assertIn(ops[0][1], (OpCode.CALL, OpCode.CALL_VAR,
                                  OpCode.CALL_FAST))
        self.assertIn(ops[1][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        self.assertEqual(body.extra_tuple(ops[1][2])[1], 1)


    async def test_begin_single_form_expression_stays_plain(self):
        # module/REPL expression streams never promote: the frame is the
        # top of the eval and its backtrace entry is worth keeping
        code = self.compile_to_code("(begin (f 1))")
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.CALL_VAR, OpCode.CALL_FAST))
        self.assertEqual(code.extra_tuple(ops[0][2])[1], 1)


    async def test_begin_f_x_x_call_not_tail(self):
        """
        (begin (f x) x) -> f is not in tail position -> CALL or CALL_VAR/FAST
        """
        code = self.compile_to_code("(begin (f x) x)")
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.CALL, OpCode.CALL_VAR,
                                  OpCode.CALL_FAST))


    async def test_begin_x_f_x_expression_stays_plain(self):
        """
        (begin x (f x)) at expression scope stays a plain call: only
        FUNCTION-mode streams promote
        """
        code = self.compile_to_code("(begin x (f x))")
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.CALL_VAR, OpCode.CALL_FAST))
        self.assertEqual(code.extra_tuple(ops[0][2])[1], 1)


class TestCallBytecodeShape(SpecialsTestCase):
    """
    call emits correct opcodes for positional, keyword, and star forms
    """

    async def test_positional_zero_args_emits_call_zero(self):
        code = self.compile_to_code("(lambda [] (f))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        self.assertEqual(body.extra_tuple(ops[0][2])[1], 0)


    async def test_positional_three_args_emits_call_three(self):
        code = self.compile_to_code("(lambda [] (f a b c))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        self.assertEqual(body.extra_tuple(ops[0][2])[1], 3)


    async def test_positional_no_call_names_or_call_star(self):
        code = self.compile_to_code("(lambda [] (f 1 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        self.assertNotIn(OpCode.CALL_NAMES, [o[1] for o in ops])
        self.assertNotIn(OpCode.CALL_STAR, [o[1] for o in ops])


    async def test_keyword_emits_call_names(self):
        code = self.compile_to_code("(lambda [] (f 1 name: 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR_NAMES,
                                  OpCode.TAIL_CALL_FAST_NAMES))
        self.assertEqual(body.extra_tuple(ops[0][2])[2], 1)


    async def test_keyword_names_tuple_in_call_row(self):
        # the fused tail twin carries the kw names tuple in its row
        code = self.compile_to_code("(lambda [] (f 1 name: 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR_NAMES,
                                  OpCode.TAIL_CALL_FAST_NAMES))
        names = body.constant(body.extra_tuple(ops[0][2])[1])
        self.assertEqual(names, ("name",))


    async def test_star_arg_emits_call_star(self):
        code = self.compile_to_code("(lambda [] (f *: rest))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR_STAR,
                                  OpCode.TAIL_CALL_FAST_STAR))
        self.assertEqual(body.extra_tuple(ops[0][2])[1], 1)


    async def test_star_arg_with_positional_emits_build_args_and_combine(self):
        code = self.compile_to_code("(lambda [] (f a b *: more))")
        body = self.lambda_body_code(code)
        arg_ops = self.arg_building_ops(body)
        call_ops = self.call_ops(body)
        self.assertEqual(len(call_ops), 1)
        self.assertIn(call_ops[0][1], (OpCode.TAIL_CALL_VAR_STAR,
                                       OpCode.TAIL_CALL_FAST_STAR))
        self.assertIn(OpCode.BUILD_ARGS, [o[1] for o in arg_ops])
        self.assertIn(OpCode.COMBINE_ARGS, [o[1] for o in arg_ops])


    async def test_full_varargs_emits_call_star_with_positional_and_keywords(self):
        """
        (fn a b *: more x: xval y: yval **: kwargs) uses the star tail
        twin with opa=3
        """
        code = self.compile_to_code(
            "(lambda [] (f a b *: more x: xval y: yval **: kwargs))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR_STAR,
                                  OpCode.TAIL_CALL_FAST_STAR))
        self.assertEqual(body.extra_tuple(ops[0][2])[1], 3)


    async def test_interleaved_positional_and_star_emits_call_star(self):
        """
        (f a *: bargs c *: dargs) uses CALL_STAR with COMBINE_ARGS 4
        """
        code = self.compile_to_code(
            "(lambda [] (f a *: bargs c *: dargs))")
        body = self.lambda_body_code(code)
        arg_ops = self.arg_building_ops(body)
        call_ops = self.call_ops(body)
        self.assertEqual(len(call_ops), 1)
        self.assertIn(call_ops[0][1], (OpCode.TAIL_CALL_VAR_STAR,
                                       OpCode.TAIL_CALL_FAST_STAR))
        self.assertIn(OpCode.BUILD_ARGS, [o[1] for o in arg_ops])
        self.assertIn(OpCode.COMBINE_ARGS, [o[1] for o in arg_ops])
        combine_ops = [o for o in arg_ops if o[1] == OpCode.COMBINE_ARGS]
        self.assertEqual(len(combine_ops), 1)
        self.assertEqual(combine_ops[0][2], 4)


    async def test_interleaved_keyword_and_starstar_emits_call_star(self):
        """
        (f x: 1 **: kw1 y: 2 **: kw2) uses CALL_STAR with COMBINE_KWDS 4
        """
        code = self.compile_to_code(
            "(lambda [] (f x: 1 **: kw1 y: 2 **: kw2))")
        body = self.lambda_body_code(code)
        arg_ops = self.arg_building_ops(body)
        call_ops = self.call_ops(body)
        self.assertEqual(len(call_ops), 1)
        self.assertIn(call_ops[0][1], (OpCode.TAIL_CALL_VAR_STAR,
                                       OpCode.TAIL_CALL_FAST_STAR))
        self.assertIn(OpCode.BUILD_KWDS_NAMES, [o[1] for o in arg_ops])
        self.assertIn(OpCode.COMBINE_KWDS, [o[1] for o in arg_ops])
        combine_ops = [o for o in arg_ops if o[1] == OpCode.COMBINE_KWDS]
        self.assertEqual(len(combine_ops), 1)
        self.assertEqual(combine_ops[0][2], 4)


    async def test_pure_positional_uses_call_not_call_star(self):
        """
        (f 1 2 3) uses the plain-call tail twin, not a names or star form
        """
        code = self.compile_to_code("(lambda [] (f 1 2 3))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))
        all_ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertNotIn(OpCode.CALL_NAMES, all_ops)
        self.assertNotIn(OpCode.CALL_STAR, all_ops)


    async def test_positional_and_keyword_uses_call_names_not_call_star(self):
        """
        (f 1 name: 2) uses the names tail twin, not a star form
        """
        code = self.compile_to_code("(lambda [] (f 1 name: 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR_NAMES,
                                  OpCode.TAIL_CALL_FAST_NAMES))
        all_ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertNotIn(OpCode.CALL_STAR, all_ops)


class TestCallVarPath(SpecialsTestCase):
    """
    When callable is a simple symbol and not in tail position, emit CALL_VAR*
    """

    async def test_symbol_callable_non_tail_emits_call_var(self):
        """
        (begin (f 1) 2) with tc: f is symbol, not tail -> CALL_VAR or CALL_FAST
        """
        code = self.compile_to_code("(lambda [] (begin (f 1) 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.CALL_VAR, OpCode.CALL_FAST),
            "non-tail symbol call should use CALL_VAR or CALL_FAST")


    async def test_symbol_callable_positional_no_tc_emits_call_var(self):
        """
        (begin (f 1 2) 2): f not in tail -> CALL_VAR or CALL_FAST
        """
        code = self.compile_to_code("(lambda [] (begin (f 1 2) 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.CALL_VAR, OpCode.CALL_FAST))


    async def test_symbol_callable_keyword_no_tc_emits_call_var_names(self):
        """
        (begin (f 1 name: 2) 2): f not in tail -> CALL_VAR_NAMES or CALL_FAST_NAMES
        """
        code = self.compile_to_code(
            "(lambda [] (begin (f 1 name: 2) 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.CALL_VAR_NAMES, OpCode.CALL_FAST_NAMES))


    async def test_symbol_callable_star_no_tc_emits_call_var_star(self):
        """
        (begin (f *: rest) 2): f not in tail -> CALL_VAR_STAR or CALL_FAST_STAR
        """
        code = self.compile_to_code("(lambda [] (begin (f *: rest) 2))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.CALL_VAR_STAR, OpCode.CALL_FAST_STAR))


    async def test_tail_position_still_emits_tail_call(self):
        """
        Symbol in tail position -> the fused tail twin, not a plain
        CALL_VAR or CALL_FAST
        """
        code = self.compile_to_code("(lambda [] (f 1))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(ops[0][1], (OpCode.TAIL_CALL_VAR,
                                  OpCode.TAIL_CALL_FAST))


    async def test_complex_callable_emits_call_attr_not_call_var(self):
        """
        (attr obj meth)(args) -> a fused attr-call opcode, not CALL_VAR
        or a generic CALL over a pre-resolved attribute
        """
        code = self.compile_to_code(
            "(lambda [] ((attr obj upper) \"hi\"))")
        body = self.lambda_body_code(code)
        ops = self.call_ops(body)
        self.assertEqual(len(ops), 1)
        self.assertIn(
            ops[0][1],
            (OpCode.CALL_ATTR,
             OpCode.CALL_VAR_ATTR,
             OpCode.CALL_FAST_ATTR,
             OpCode.TAIL_CALL_ATTR,
             OpCode.TAIL_CALL_VAR_ATTR,
             OpCode.TAIL_CALL_FAST_ATTR),
            "complex attr callable should use a fused attr-call opcode")


class TestCallEvaluationOrder(SpecialsTestCase):
    """
    Arguments are evaluated left-to-right; bytecode and runtime order
    """

    async def test_positional_args_precede_call(self):
        code = self.compile_to_code("(lambda [] (f 1 2))")
        body = self.lambda_body_code(code)
        call_idx = None
        const_count = 0
        for i in range(body.length()):
            op, arg = body.oparg(i)
            if op in self.CALL_OPCODES:
                call_idx = i
            elif op == OpCode.CONST:
                const_count += 1
            elif op == OpCode.CONST2:
                const_count += 2
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

        src = '(call f (call tag "a") (call tag "b"))'
        expr, mod = self.compile_expr(src, f=spy, tag=tag)
        await expr()
        self.assertEqual(received[0], "a")
        self.assertEqual(received[1], "b")
        self.assertEqual(received[2], (["a", "b"], {}))


class TestCallEndToEnd(SpecialsTestCase):
    """
    Compiled (call f ...) runs and passes args/kwargs to callable
    """

    async def test_call_zero_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return 42

        expr, mod = self.compile_expr("(call f)", f=spy)
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

        expr, mod = self.compile_expr("(call f 1)", f=spy)
        await expr()
        self.assertEqual(got[0], ((1,), {}))


    async def test_call_three_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(call f 1 2 3)", f=spy)
        await expr()
        self.assertEqual(got[0], ((1, 2, 3), {}))


    async def test_call_keyword_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(call f 1 name: 2)", f=spy)
        await expr()
        self.assertEqual(got[0], ((1,), {"name": 2}))


    async def test_call_keyword_only_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr("(call f name: 2)", f=spy)
        await expr()
        self.assertEqual(got[0], ((), {"name": 2}))


    async def test_call_var_args(self):
        got = []

        def spy(*args, **kwargs):
            got.append((args, kwargs))
            return None

        expr, mod = self.compile_expr(
            "(call f *: more)",
            f=spy, more=(1, 2, 3))
        await expr()
        self.assertEqual(got[0], ((1, 2, 3), {}))


    async def test_call_interleaved_positional_and_star_end_to_end(self):
        got = []

        def spy(*args, **kwargs):
            got.append((list(args), dict(kwargs)))
            return None

        src = "(call f 1 *: mid 2 *: end)"
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
        Else branch is a call in tail position (would promote to a tail
        twin in a non-yielding body). With yield in the same body, final
        code keeps the plain call form.
        """

        code = self.compile_to_code(
            """
            (lambda [x]
              (if x
                (yield x)
                (call list 1 2 3)))
            """,
            list=list,
        )
        body = self.lambda_body_code(code)
        ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertIn(OpCode.YIELD, ops)
        for op in ops:
            self.assertNotIn(op, TAIL_CALL_OPCODES)
        self.assertTrue(any(op in (OpCode.CALL, OpCode.CALL_VAR,
                                   OpCode.CALL_FAST)
                            for op in ops))


class TestStarArgNonIterable(SpecialsTestCase):
    """
    (f *: x) with a non-iterable x raises TypeError at run time
    """

    async def test_direct_call(self):
        expr, mod = self.compile_expr(
            "(f *: five)", f=lambda *a: a, five=5)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("argument after *: must be an iterable, not int",
                      str(ctx.exception))


    async def test_method_call(self):
        expr, mod = self.compile_expr(
            "(text.split *: nothing)", text="a-b", nothing=None)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("not NoneType", str(ctx.exception))


    async def test_tail_position(self):
        expr, mod = self.compile_expr(
            "((lambda [] (f *: five)))", f=lambda *a: a, five=5)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("not int", str(ctx.exception))


    async def test_iterable_star_still_works(self):
        expr, mod = self.compile_expr(
            "(f *: items)", f=lambda *a: a, items=range(3))
        self.assertEqual((0, 1, 2), await expr())


class TestStarStarArgNonMapping(SpecialsTestCase):
    """
    (f **: x) with a non-mapping x raises TypeError at run time, for a
    Koskari callee and a Python callee alike
    """

    async def test_koskari_callee(self):
        expr, mod = self.compile_expr(
            "((lambda [**: k] k) **: five)", five=5)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("argument after **: must be a mapping, not int",
                      str(ctx.exception))


    async def test_python_callee(self):
        expr, mod = self.compile_expr("(f **: five)", f=dict, five=5)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("not int", str(ctx.exception))


    async def test_two_chunks(self):
        expr, mod = self.compile_expr(
            '(f **: #{"a": 1} **: five)', f=dict, five=5)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("not int", str(ctx.exception))


    async def test_non_dict_mapping_is_accepted(self):
        import types
        expr, mod = self.compile_expr(
            "(f **: proxy)", f=dict,
            proxy=types.MappingProxyType({"a": 1}))
        self.assertEqual({"a": 1}, await expr())


# The end.
