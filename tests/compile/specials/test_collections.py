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
Tests for koskari.compile.specials.collections #list and
#list-append.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def _opcodes(code):
    return [code.oparg(i)[0] for i in range(code.length())]


def _find_op(code, opcode):
    for i in range(code.length()):
        op, arg = code.oparg(i)
        if op == opcode:
            return arg
    return None


class TestBuildListSpecial(SpecialsTestCase):

    async def test_list_empty(self):
        expr, mod = self.compile_expr("(#list)")
        self.assertEqual([], await expr())


    async def test_list_empty_emits_build_list(self):
        code = self.compile_to_code("(#list)")
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST, ops)
        self.assertEqual(0, _find_op(code, OpCode.BUILD_LIST))


    async def test_list_values(self):
        expr, mod = self.compile_expr("(#list 1 2 3)")
        self.assertEqual([1, 2, 3], await expr())


    async def test_list_all_const_emits_build_list_const(self):
        # an all-constant list folds to a single BUILD_LIST_CONST, no
        # per-element CONST loads and no dynamic BUILD_LIST
        code = self.compile_to_code("(#list 1 2 3)")
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST_CONST, ops)
        self.assertNotIn(OpCode.BUILD_LIST, ops)
        self.assertNotIn(OpCode.CONST, ops)
        self.assertEqual([], self.call_ops(code))


    async def test_list_single_const_folds(self):
        expr, mod = self.compile_expr("(#list 42)")
        self.assertEqual([42], await expr())
        code = self.compile_to_code("(#list 42)")
        self.assertIn(OpCode.BUILD_LIST_CONST, _opcodes(code))


    async def test_list_pure_dynamic_uses_build_list(self):
        expr, mod = self.compile_expr("(#list x y)", x=1, y=2)
        self.assertEqual([1, 2], await expr())
        code = self.compile_to_code("(#list x y)", x=1, y=2)
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST, ops)
        self.assertEqual(2, _find_op(code, OpCode.BUILD_LIST))
        self.assertNotIn(OpCode.BUILD_LIST_APPEND, ops)


    async def test_list_mixed_runs_value(self):
        expr, mod = self.compile_expr("(#list 1 x 2)", x=99)
        self.assertEqual([1, 99, 2], await expr())


    async def test_list_mixed_runs_emits_accumulator(self):
        code = self.compile_to_code("(#list 1 x 2)", x=99)
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST_CONST, ops)
        self.assertIn(OpCode.BUILD_LIST_APPEND, ops)
        self.assertIn(OpCode.BUILD_LIST_APPEND_CONST, ops)


    async def test_list_worked_example(self):
        # (#list 1 2 3 a b 4 5 6 z 8 9): const run, dynamic run, const run,
        # single dynamic, const run
        src = "(#list 1 2 3 a b 4 5 6 z 8 9)"
        expr, mod = self.compile_expr(src, a=10, b=20, z=30)
        self.assertEqual([1, 2, 3, 10, 20, 4, 5, 6, 30, 8, 9], await expr())
        code = self.compile_to_code(src, a=10, b=20, z=30)
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST_CONST, ops)
        self.assertIn(OpCode.BUILD_LIST_EXTEND_CONST, ops)
        self.assertIn(OpCode.BUILD_LIST_APPEND, ops)


class TestBuildTupleSpecial(SpecialsTestCase):

    async def test_tuple_empty(self):
        expr, mod = self.compile_expr("(#tuple)")
        self.assertEqual((), await expr())


    async def test_tuple_all_const_emits_single_const(self):
        expr, mod = self.compile_expr("(#tuple 1 2 3)")
        self.assertEqual((1, 2, 3), await expr())
        code = self.compile_to_code("(#tuple 1 2 3)")
        ops = _opcodes(code)
        self.assertTrue({OpCode.CONST, OpCode.RETURN_CONST} & set(ops))
        self.assertNotIn(OpCode.BUILD_ARGS, ops)
        self.assertNotIn(OpCode.COMBINE_ARGS, ops)


    async def test_tuple_pure_dynamic_uses_build_args(self):
        expr, mod = self.compile_expr("(#tuple x y)", x=1, y=2)
        self.assertEqual((1, 2), await expr())
        # out of tail position: a tail tuple folds into RETURN n, which
        # builds the same tuple
        code = self.compile_to_code("(begin (#tuple x y) None)", x=1, y=2)
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_ARGS, ops)
        self.assertNotIn(OpCode.COMBINE_ARGS, ops)


    async def test_tuple_mixed_runs_value(self):
        expr, mod = self.compile_expr("(#tuple 1 x 2)", x=99)
        self.assertEqual((1, 99, 2), await expr())


    async def test_tuple_mixed_runs_emits_combine(self):
        code = self.compile_to_code("(#tuple 1 x 2)", x=99)
        ops = _opcodes(code)
        self.assertIn(OpCode.COMBINE_ARGS, ops)


class TestCollectionSplat(SpecialsTestCase):

    async def test_list_trailing_splat_value(self):
        expr, mod = self.compile_expr("(#list 1 2 3 *: a)", a=[4, 5])
        self.assertEqual([1, 2, 3, 4, 5], await expr())


    async def test_list_trailing_splat_emits_extend(self):
        code = self.compile_to_code("(#list 1 2 3 *: a)", a=[4, 5])
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST_CONST, ops)
        self.assertIn(OpCode.BUILD_LIST_EXTEND, ops)
        self.assertEqual([], self.call_ops(code))


    async def test_list_leading_splat_value(self):
        expr, mod = self.compile_expr("(#list *: a 4 5)", a=[1, 2])
        self.assertEqual([1, 2, 4, 5], await expr())


    async def test_list_only_splat_value(self):
        expr, mod = self.compile_expr("(#list *: a)", a=(1, 2, 3))
        self.assertEqual([1, 2, 3], await expr())


    async def test_list_only_splat_is_fresh_copy(self):
        # the result must be a new list, not the splatted source
        src = "(#list *: a)"
        source = [1, 2]
        expr, mod = self.compile_expr(src, a=source)
        result = await expr()
        self.assertEqual([1, 2], result)
        self.assertIsNot(result, source)
        result.append(9)
        self.assertEqual([1, 2], source)


    async def test_list_splat_of_iterable(self):
        expr, mod = self.compile_expr("(#list 0 *: a)", a=range(1, 4))
        self.assertEqual([0, 1, 2, 3], await expr())


    async def test_list_dynamic_then_splat(self):
        expr, mod = self.compile_expr("(#list x *: a y)", x=1, a=[2, 3], y=4)
        self.assertEqual([1, 2, 3, 4], await expr())
        code = self.compile_to_code("(#list x *: a y)", x=1, a=[2, 3], y=4)
        ops = _opcodes(code)
        self.assertIn(OpCode.BUILD_LIST, ops)
        self.assertIn(OpCode.BUILD_LIST_EXTEND, ops)
        self.assertIn(OpCode.BUILD_LIST_APPEND, ops)


    async def test_tuple_trailing_splat_value(self):
        expr, mod = self.compile_expr("(#tuple 1 2 *: a)", a=[3, 4])
        self.assertEqual((1, 2, 3, 4), await expr())


    async def test_tuple_trailing_splat_emits_combine(self):
        code = self.compile_to_code("(#tuple 1 2 *: a)", a=[3, 4])
        ops = _opcodes(code)
        self.assertIn(OpCode.COMBINE_ARGS, ops)
        self.assertEqual([], self.call_ops(code))


    async def test_tuple_only_splat_coerces_to_tuple(self):
        expr, mod = self.compile_expr("(#tuple *: a)", a=[1, 2, 3])
        result = await expr()
        self.assertEqual((1, 2, 3), result)
        self.assertIsInstance(result, tuple)
        code = self.compile_to_code("(#tuple *: a)", a=[1, 2, 3])
        self.assertIn(OpCode.COMBINE_ARGS, _opcodes(code))


    async def test_kwarg_form_reroutes_to_call(self):
        # keyword arguments cannot build inline; the special defers to a
        # runtime call of the builtin
        code = self.compile_to_code("(#list key: 1)")
        self.assertNotEqual([], self.call_ops(code))


class TestListAppendSpecial(SpecialsTestCase):

    async def test_list_append_symbol(self):
        expr, mod = self.compile_expr(
            "(begin (setq col (#list)) (#list-append col 9) col)")
        self.assertEqual([9], await expr())


    async def test_list_append_symbol_emits_opcode(self):
        code = self.compile_to_code(
            "(begin (setq col (#list)) (#list-append col 1))")
        ops = _opcodes(code)
        self.assertTrue(
            OpCode.VAR_LIST_APPEND in ops
            or OpCode.FAST_LIST_APPEND in ops)
        self.assertEqual([], self.call_ops(code))


    async def test_list_append_expr(self):
        expr, mod = self.compile_expr("(#list-append (#list 1) 2)")
        self.assertEqual([1, 2], await expr())


    async def test_list_append_expr_emits_build_list_append(self):
        code = self.compile_to_code("(#list-append (#list 1) 2)")
        ops = _opcodes(code)
        # (#list 1) folds to BUILD_LIST_CONST; the append rides the
        # single-value BUILD_LIST_APPEND (count 1)
        self.assertIn(OpCode.BUILD_LIST_CONST, ops)
        self.assertIn(OpCode.BUILD_LIST_APPEND, ops)
        self.assertEqual(1, _find_op(code, OpCode.BUILD_LIST_APPEND))


class TestListEachPattern(SpecialsTestCase):

    async def test_let_foreach_list_append_pattern(self):
        src = """
        (let [[col (#list)]]
          (for-each [i x]
            (#list-append col i))
          col)
        """
        expr, mod = self.compile_expr(src, x=range(3))
        self.assertEqual([0, 1, 2], await expr())


    async def test_let_foreach_list_append_no_calls(self):
        src = """
        (let [[col (#list)]]
          (for-each [i x]
            (#list-append col i))
          col)
        """
        code = self.compile_to_code(src, x=range(2))
        self.assertEqual([], self.call_ops(code))


class TestDictLiteral(SpecialsTestCase):
    """
    The #{ } literal lowers to a plain runtime #dict call with
    (#tuple KEY VALUE) entry arguments; there is no #dict
    special.
    """

    async def test_dict_empty(self):
        expr, mod = self.compile_expr("#{}")
        self.assertEqual({}, await expr())


    async def test_dict_string_keys(self):
        expr, mod = self.compile_expr('#{"foo": 123 "bar": "tacos"}')
        self.assertEqual({"foo": 123, "bar": "tacos"}, await expr())


    async def test_dict_const_entries_fold(self):
        # a constant entry folds into a pooled CONST tuple; the literal
        # is one call to #dict, not an entry-building opcode program
        code = self.compile_to_code('#{"foo": 123}')
        ops = _opcodes(code)
        self.assertNotIn(OpCode.BUILD_KWDS, ops)
        self.assertNotIn(OpCode.COMBINE_KWDS, ops)
        self.assertEqual(1, len(self.call_ops(code)))


    async def test_dict_keyword_key_is_expression(self):
        # #{foo: bar} resolves foo as an expression, matching Python's
        # { } literal; the string-key spelling is the direct #dict call
        src = '(let [[foo "F"]] #{foo: "B"})'
        expr, mod = self.compile_expr(src)
        self.assertEqual({"F": "B"}, await expr())


    async def test_dict_int_keyword_key(self):
        expr, mod = self.compile_expr('#{5: "five"}')
        self.assertEqual({5: "five"}, await expr())


    async def test_dict_expression_values(self):
        expr, mod = self.compile_expr('#{"n": (+ 1 2)}')
        self.assertEqual({"n": 3}, await expr())


    async def test_dict_splat_merges_last(self):
        src = '(let [[m #{"b": 2}]] #{"a": 1 "b": 99 **: m})'
        expr, mod = self.compile_expr(src)
        self.assertEqual({"a": 1, "b": 2}, await expr())


    async def test_dict_splat_nonstring_keys(self):
        src = '(let [[m #{1: "one"}]] #{2: "two" **: m})'
        expr, mod = self.compile_expr(src)
        self.assertEqual({2: "two", 1: "one"}, await expr())


    async def test_dict_direct_call_keyword_args(self):
        # in a direct call, keywords are call kwargs: string keys
        expr, mod = self.compile_expr('(#dict foo: 1 bar: 2)')
        self.assertEqual({"foo": 1, "bar": 2}, await expr())


# The end.
