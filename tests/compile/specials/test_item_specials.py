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
Tests for koskari.compile item, del-item, and set-item
special forms.
"""


import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.lib import OpCode
from koskari.matcher import CALLEDBY, MATCH
from koskari.syntax import special

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestItemSpecial(SpecialsTestCase):

    async def test_item_constant_key(self):
        lst = [10, 20, 30]
        src_str = "(item lst 0)"
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(10, result)


    async def test_item_variable_key(self):
        lst = [10, 20, 30]
        src_str = """
        (let [[i 2]]
          (item lst i))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(30, result)


class TestSetItemSpecial(SpecialsTestCase):

    async def test_set_item_then_read(self):
        lst = [1, 2, 3]
        src_str = """
        (begin
          (set-item lst 1 99)
          (item lst 1))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(99, result)
        self.assertEqual(99, lst[1])


class TestDelItemSpecial(SpecialsTestCase):

    async def test_del_item_returns_none(self):
        d = {"remove_me": 1}
        src_str = '(del-item d "remove_me")'
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertIsNone(result)
        self.assertNotIn("remove_me", d)


class TestSetItemShapes(SpecialsTestCase):
    """
    compile/set-item emits the object, key, value, and ITEM_STORE and
    the compress pipeline folds the trailing feeders; when the key or
    the value is not a feeder the special names what a fold could not
    reach (see design/interpreter/item-opcode-matrix.md)
    """

    def ops(self, src, **defns):
        code = self.compile_to_code(src, **defns)
        return [code.oparg(i)[0] for i in range(code.length())]

    async def test_all_feeders_fold_to_one_row(self):
        cases = {
            '(set-item lst i v)': OpCode.VAR_ITEM_VAR_STORE_VAR,
            '(set-item lst i 1)': OpCode.VAR_ITEM_VAR_STORE_CONST,
            '(set-item d "k" v)': OpCode.VAR_ITEM_CONST_STORE_VAR,
            '(set-item d "k" 1)': OpCode.VAR_ITEM_CONST_STORE_CONST,
        }
        for src, row in cases.items():
            ops = self.ops(src, lst=[], d={}, i=0, v=0)
            self.assertIn(row, ops, src)
            self.assertNotIn(OpCode.VAR, ops, src)
            self.assertNotIn(OpCode.CONST, ops[:ops.index(row)], src)

    async def test_compound_object_folds_key_and_value(self):
        cases = {
            '(set-item (f) i v)': OpCode.ITEM_VAR_STORE_VAR,
            '(set-item (f) i 1)': OpCode.ITEM_VAR_STORE_CONST,
            '(set-item (f) "k" v)': OpCode.ITEM_CONST_STORE_VAR,
            '(set-item (f) "k" 1)': OpCode.ITEM_CONST_STORE_CONST,
            '(set-item o.a i v)': OpCode.ITEM_VAR_STORE_VAR,
        }
        for src, row in cases.items():
            ops = self.ops(src, f=None, o=None, i=0, v=0)
            self.assertIn(row, ops, src)
            self.assertNotIn(OpCode.ITEM_STORE, ops, src)

    async def test_symbol_object_under_a_computed_key_is_named(self):
        ops = self.ops("(set-item lst (+ i 1) v)", lst=[], i=0, v=0)
        self.assertIn(OpCode.VAR_ITEM_STORE_VAR, ops)
        ops = self.ops("(set-item lst (+ i 1) 1)", lst=[], i=0)
        self.assertIn(OpCode.VAR_ITEM_STORE_CONST, ops)
        ops = self.ops("(set-item lst (+ i 1) (f))", lst=[], i=0, f=None)
        self.assertIn(OpCode.VAR_ITEM_STORE, ops)
        self.assertNotIn(OpCode.ITEM_STORE, ops)

    async def test_computed_value_behind_named_operands(self):
        cases = {
            '(set-item lst i (f))': OpCode.VAR_ITEM_VAR_STORE,
            '(set-item d "k" (f))': OpCode.VAR_ITEM_CONST_STORE,
            '(set-item (g) i (f))': OpCode.ITEM_VAR_STORE,
            '(set-item (g) "k" (f))': OpCode.ITEM_CONST_STORE,
        }
        for src, row in cases.items():
            ops = self.ops(src, lst=[], d={}, i=0, f=None, g=None)
            self.assertIn(row, ops, src)
            self.assertNotIn(OpCode.ITEM_STORE, ops, src)

    async def test_nothing_to_name_stays_plain(self):
        ops = self.ops("(set-item (g) (h) (f))", f=None, g=None, h=None)
        self.assertIn(OpCode.ITEM_STORE, ops)
        ops = self.ops("(set-item (g) (h) v)", g=None, h=None, v=0)
        self.assertIn(OpCode.ITEM_STORE_VAR, ops)
        ops = self.ops("(set-item (g) (h) 1)", g=None, h=None)
        self.assertIn(OpCode.ITEM_STORE_CONST, ops)

    async def test_function_body_lowers_to_fast(self):
        cases = {
            "(lambda [lst i v] (set-item lst i v))": OpCode.FAST_ITEM_FAST_STORE_FAST,
            "(lambda [lst i] (set-item lst i 1))": OpCode.FAST_ITEM_FAST_STORE_CONST,
            "(lambda [lst i v] (set-item lst (+ i 1) v))": OpCode.FAST_ITEM_STORE_FAST,
            "(lambda [o i v] (set-item o.a i v))": OpCode.ITEM_FAST_STORE_FAST,
            "(lambda [o i] (set-item o.a i 1))": OpCode.ITEM_FAST_STORE_CONST,
        }
        for src, row in cases.items():
            body = self.lambda_body_code(self.compile_to_code(src))
            ops = [body.oparg(i)[0] for i in range(body.length())]
            self.assertIn(row, ops, src)

    async def test_stores_land(self):
        lst = [1, 2, 3]
        src_str = """
        (let [[i 2]]
          (set-item lst i 99)
          (item lst i))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        self.assertEqual(99, await expr())
        self.assertEqual([1, 2, 99], lst)
        d = {}
        expr, mod = self.compile_expr("(set-item d \"k\" 5)", d=d)
        self.assertIsNone(await expr())
        self.assertEqual({"k": 5}, d)
        grid = [[0, 0], [0, 0]]
        expr, mod = self.compile_expr(
            "(let [[x 1] [y 0]] (set-item (item grid y) x 7) grid)", grid=grid)
        self.assertEqual([[0, 7], [0, 0]], await expr())

    async def test_named_object_resolves_when_the_row_runs(self):
        # the row fetches a named object when it runs, after every
        # stack operand: a value expression that rebinds the name
        # stores into the new binding
        old, new = [0], [0]
        src_str = """
        (let [[d old]]
          (set-item d 0 (begin (setq d new) 7)))
        """
        expr, mod = self.compile_expr(src_str, old=old, new=new)
        await expr()
        self.assertEqual([0], old)
        self.assertEqual([7], new)

    async def test_unbound_object_raises_after_the_value(self):
        seen = []
        src_str = "(set-item missing 0 (note 1))"
        expr, mod = self.compile_expr(src_str, note=seen.append)
        with self.assertRaises(NameError):
            await expr()
        self.assertEqual([1], seen)

    async def test_captured_operands_take_the_deref_path(self):
        # a captured name sits in a ref-kind slot the peek does not
        # read: every row resolves it through the general deref
        src_str = """
        (let [[lst (#list 0 0 0)] [i 1] [v 5]]
          (define f (lambda [] (set-item lst i v) (set-item lst 2 v)
                               (inplace-item + lst i 1) (item lst i)))
          (#tuple (f) lst))
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual((6, [0, 6, 5]), await expr())


class TestItemShapes(SpecialsTestCase):
    """
    compile/item, del-item, take-item, and ref-item emit the object,
    the key, and the stack row; the fold names feeders, and a symbol
    object under a computed key is named by the special
    """

    def ops(self, src, **defns):
        code = self.compile_to_code(src, **defns)
        return [code.oparg(i)[0] for i in range(code.length())]

    async def test_folds(self):
        cases = {
            "(item lst i)": OpCode.VAR_ITEM_VAR,
            "(item lst 0)": OpCode.VAR_ITEM_CONST,
            "(item (f) i)": OpCode.ITEM_VAR,
            "(item (f) 0)": OpCode.ITEM_CONST,
            "(item o.a i)": OpCode.ITEM_VAR,
            "(del-item lst i)": OpCode.VAR_ITEM_VAR_CLEAR,
            "(del-item (f) 0)": OpCode.ITEM_CONST_CLEAR,
            "(take-item lst 0)": OpCode.VAR_ITEM_CONST_TAKE,
            "(take-item (f) i)": OpCode.ITEM_VAR_TAKE,
            "(ref-item lst i)": OpCode.VAR_ITEM_VAR_REF,
            "(ref-item (f) 0)": OpCode.ITEM_CONST_REF,
        }
        for src, row in cases.items():
            ops = self.ops(src, lst=[], i=0, f=None, o=None)
            self.assertIn(row, ops, src)
            self.assertNotIn(OpCode.ITEM, ops, src)

    async def test_symbol_object_under_a_computed_key_is_named(self):
        cases = {
            "(item lst (+ i 1))": OpCode.VAR_ITEM,
            "(del-item lst (+ i 1))": OpCode.VAR_ITEM_CLEAR,
            "(take-item lst (+ i 1))": OpCode.VAR_ITEM_TAKE,
            "(ref-item lst (+ i 1))": OpCode.VAR_ITEM_REF,
        }
        for src, row in cases.items():
            ops = self.ops(src, lst=[], i=0)
            self.assertIn(row, ops, src)

    async def test_nothing_to_name_stays_plain(self):
        ops = self.ops("(item (f) (g))", f=None, g=None)
        self.assertIn(OpCode.ITEM, ops)

    async def test_function_body_lowers_to_fast(self):
        cases = {
            "(lambda [lst i] (item lst i))": OpCode.FAST_ITEM_FAST,
            "(lambda [lst i] (item lst (+ i 1)))": OpCode.FAST_ITEM,
            "(lambda [o i] (item o.a i))": OpCode.ITEM_FAST,
            "(lambda [lst i] (take-item lst i))": OpCode.FAST_ITEM_FAST_TAKE,
            "(lambda [lst i] (del-item lst (+ i 1)))": OpCode.FAST_ITEM_CLEAR,
            "(lambda [o i] (ref-item o.a i))": OpCode.ITEM_FAST_REF,
        }
        for src, row in cases.items():
            body = self.lambda_body_code(self.compile_to_code(src))
            ops = [body.oparg(i)[0] for i in range(body.length())]
            self.assertIn(row, ops, src)

    async def raised_by(self, src, **defns):
        expr, mod = self.compile_expr(src, **defns)
        try:
            await expr()
        except Exception as e:
            return type(e)
        return None


    async def test_nil_object_is_a_constant_not_a_name(self):
        # nil reads as a symbol but compiles to a constant: under a
        # computed key it stays on the stack and the container's own
        # error is raised, as the feeder-key form raises it
        cases = {
            "(item nil (k))": "(item nil 0)",
            "(del-item nil (k))": "(del-item nil 0)",
            "(take-item nil (k))": "(take-item nil 0)",
            "(ref-item nil (k))": "(ref-item nil 0)",
            "(set-item nil (k) 1)": "(set-item nil 0 1)",
            "(set-item nil (k) (f))": "(set-item nil 0 1)",
        }
        for shaped, plain in cases.items():
            ops = self.ops(shaped, k=lambda: 0, f=lambda: 1)
            self.assertFalse(
                [op for op in ops if OpCode(op).name.startswith("VAR_ITEM")], shaped)
            expected = await self.raised_by(plain)
            self.assertIsNot(expected, NameError, plain)
            self.assertIs(
                await self.raised_by(shaped, k=lambda: 0, f=lambda: 1),
                expected, shaped)


    async def test_accesses_land(self):
        src_str = """
        (let [[lst (#list 10 20 30)] [d (#dict (#tuple "k" 1))] [i 1]]
          (define f (lambda [o] (#tuple (item o i) (item lst (+ i 1)))))
          (define fr (f lst))
          (define r (ref-item lst (+ i 1)))
          (define rv (r.deref))
          (define t (take-item d "k"))
          (del-item lst i)
          (#tuple fr rv t lst d))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(((20, 30), 30, 1, [10, 30], {}), result)


class TestItemKeyCompiledOnce(SpecialsTestCase):
    """
    The key form of an item store compiles once whatever path the
    store takes, so a compile-time effect in the key runs once
    """

    def counting_specials(self):
        counts = []
        patterns = [MATCH(CALLEDBY("called_by"))]

        @special("compile/count-key", patterns=patterns, unpack="kwargs")
        def special_count_key(compiler, called_by):
            # a computed key: neither a lone constant nor a lone name,
            # so the store takes its stack path
            counts.append(1)
            frag = compiler.fragment(called_by.datum)
            frag.instruction(OpCode.CONST, 0)
            frag.instruction(OpCode.CONST, 1)
            frag.instruction(OpCode.POP, 1)
            return frag

        specials = dict(_BUILTIN_REGISTRY)
        specials["compile/count-key"] = special_count_key
        return specials, counts


    async def test_set_item_symbol_obj_computed_key_compiles_key_once(self):
        specials, counts = self.counting_specials()
        self.compile_expr(
            "(set-item lst (count-key) 1)", specials=specials, lst=[])
        self.assertEqual(len(counts), 1)


    async def test_inplace_item_symbol_obj_computed_key_compiles_key_once(self):
        specials, counts = self.counting_specials()
        self.compile_expr(
            "(inplace-item + lst (count-key) 1)",
            specials=specials, lst=[0])
        self.assertEqual(len(counts), 1)


# The end.
