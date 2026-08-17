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
Tests for koskari.compile quasiquote special form, covering:
- atom quasi-quote (symbols, keywords, numbers)
- proper list quasi-quote (all-constant tuple-optimisation path)
- unquote (,) in various positions
- unquote-splicing (,@) single and multiple splice sites
- dotted pair with unquote tail
- nested quasiquote (level > 0 keeps inner unquote as data)
- unquote and unquote-splicing outside quasiquote raise SpecialError

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.exceptions import SpecialError
from koskari.lib import cons, nil, symbol

from . import SpecialsTestCase, _register_specials

import pytest

pytestmark = pytest.mark.phase_specials


class TestQuasiquoteAtoms(SpecialsTestCase):
    """
    Quasi-quoting atomic values
    """

    async def test_quasiquote_symbol(self):
        """
        `foo yields the symbol foo
        """

        expr, mod = self.compile_expr("`foo")
        result = await expr()
        self.assertEqual(result, symbol("foo"))


    async def test_quasiquote_number(self):
        """
        `42 yields the integer 42
        """

        expr, mod = self.compile_expr("`42")
        result = await expr()
        self.assertEqual(result, 42)


    async def test_quasiquote_nil(self):
        """
        `() yields nil
        """

        expr, mod = self.compile_expr("`()")
        result = await expr()
        self.assertIs(result, nil)


class TestQuasiquoteProperList(SpecialsTestCase):
    """
    Quasi-quoting proper lists with no unquote (all-constant tuple path)
    """

    async def test_quasiquote_simple_list(self):
        """
        `(a b c) yields (a b c) as a proper list
        """

        expr, mod = self.compile_expr("`(a b c)")
        result = await expr()
        self.assertEqual(
            [symbol("a"), symbol("b"), symbol("c")],
            list(result.unpack()))


    async def test_quasiquote_nested_list(self):
        """
        `(a (b c)) yields a nested proper list
        """

        expr, mod = self.compile_expr("`(a (b c))")
        result = await expr()
        outer = list(result.unpack())
        self.assertEqual(symbol("a"), outer[0])
        self.assertEqual(
            [symbol("b"), symbol("c")],
            list(outer[1].unpack()))


    async def test_quasiquote_list_is_proper(self):
        """
        `(a b) produces a proper list
        """

        expr, mod = self.compile_expr("`(a b)")
        result = await expr()
        self.assertTrue(result.is_proper())


class TestQuasiquoteUnquote(SpecialsTestCase):
    """
    Quasi-quoting lists containing unquote (,expr)
    """

    async def test_unquote_single_element(self):
        """
        `(,x) with x=99 yields (99)
        """

        expr, mod = self.compile_expr("`(,x)", x=99)
        result = await expr()
        self.assertEqual([99], list(result.unpack()))


    async def test_unquote_head_of_list(self):
        """
        `(,x b c) evaluates x and keeps b, c as symbols
        """

        expr, mod = self.compile_expr("`(,x b c)", x=1)
        result = await expr()
        self.assertEqual(
            [1, symbol("b"), symbol("c")],
            list(result.unpack()))


    async def test_unquote_middle_of_list(self):
        """
        `(a ,x c) evaluates x in the middle position
        """

        expr, mod = self.compile_expr("`(a ,x c)", x=2)
        result = await expr()
        self.assertEqual(
            [symbol("a"), 2, symbol("c")],
            list(result.unpack()))


    async def test_unquote_tail_of_list(self):
        """
        `(a b ,x) evaluates x as last element
        """

        expr, mod = self.compile_expr("`(a b ,x)", x=3)
        result = await expr()
        self.assertEqual(
            [symbol("a"), symbol("b"), 3],
            list(result.unpack()))


    async def test_unquote_expression(self):
        """
        `(a ,(+ x 1)) evaluates the sub-expression at runtime
        """

        expr, mod = self.compile_expr("`(a ,(+ x 1))", x=10)
        result = await expr()
        self.assertEqual(
            [symbol("a"), 11],
            list(result.unpack()))


class TestQuasiquoteSplice(SpecialsTestCase):
    """
    Quasi-quoting lists containing unquote-splicing (,@expr)
    """

    def _make_list(self, *vals):
        result = nil
        for v in reversed(vals):
            result = cons(v, result)
        return result


    async def test_splice_only(self):
        """
        `(,@lst) with lst=(1 2 3) yields (1 2 3)
        """

        lst = self._make_list(1, 2, 3)
        expr, mod = self.compile_expr("`(,@lst)", lst=lst)
        result = await expr()
        self.assertEqual([1, 2, 3], list(result.unpack()))


    async def test_splice_only_python_list(self):
        """
        Lone ,@ must still run PAIR_SPLICE so a Python list splices to a proper
        Koskari list, not a raw list object.
        """

        expr, mod = self.compile_expr("`(,@lst)", lst=[1, 2, 3])
        result = await expr()
        self.assertEqual(cons(1, 2, 3, nil), result)
        self.assertTrue(result.is_proper())


    async def test_splice_only_python_tuple(self):
        """
        Same for tuple — PAIR_SPLICE normalizes tuple segments to cons cells.
        """

        expr, mod = self.compile_expr("`(,@t)", t=(1, 2, 3))
        result = await expr()
        self.assertEqual(cons(1, 2, 3, nil), result)


    async def test_splice_python_list_after_fixed_prefix(self):
        """
        `(foo ,@lst) with lst a Python list — splices as cons cells, not nested list
        """

        expr, mod = self.compile_expr(
            "`(foo ,@lst)",
            lst=[0, 1, 2, 3, 4],
        )
        result = await expr()
        self.assertEqual(
            [symbol("foo"), 0, 1, 2, 3, 4],
            list(result.unpack()),
        )


    async def test_splice_at_front(self):
        """
        `(,@lst b c) prepends lst before b, c
        """

        lst = self._make_list(1, 2)
        expr, mod = self.compile_expr("`(,@lst b c)", lst=lst)
        result = await expr()
        self.assertEqual(
            [1, 2, symbol("b"), symbol("c")],
            list(result.unpack()))


    async def test_splice_in_middle(self):
        """
        `(a ,@lst c) inserts lst after a
        """

        lst = self._make_list(10, 20)
        expr, mod = self.compile_expr("`(a ,@lst c)", lst=lst)
        result = await expr()
        self.assertEqual(
            [symbol("a"), 10, 20, symbol("c")],
            list(result.unpack()))


    async def test_splice_at_end(self):
        """
        `(a b ,@lst) appends lst after a, b
        """

        lst = self._make_list(3, 4)
        expr, mod = self.compile_expr("`(a b ,@lst)", lst=lst)
        result = await expr()
        self.assertEqual(
            [symbol("a"), symbol("b"), 3, 4],
            list(result.unpack()))


    async def test_splice_two_sites(self):
        """
        `(,@x ,@y) with x=(1 2) and y=(3 4) yields (1 2 3 4)
        """

        x = self._make_list(1, 2)
        y = self._make_list(3, 4)
        expr, mod = self.compile_expr("`(,@x ,@y)", x=x, y=y)
        result = await expr()
        self.assertEqual([1, 2, 3, 4], list(result.unpack()))


    async def test_splice_with_unquote(self):
        """
        `(a ,v ,@lst b) mixes unquote and splice
        """

        lst = self._make_list(10, 20)
        expr, mod = self.compile_expr("`(a ,v ,@lst b)", v=99, lst=lst)
        result = await expr()
        self.assertEqual(
            [symbol("a"), 99, 10, 20, symbol("b")],
            list(result.unpack()))


    async def test_splice_empty_list(self):
        """
        `(a ,@lst b) with lst=nil yields (a b)
        """

        expr, mod = self.compile_expr("`(a ,@lst b)", lst=nil)
        result = await expr()
        self.assertEqual(
            [symbol("a"), symbol("b")],
            list(result.unpack()))


    async def test_splice_result_is_proper(self):
        """
        Splice always produces a proper list
        """

        lst = self._make_list(1, 2)
        expr, mod = self.compile_expr("`(,@lst)", lst=lst)
        result = await expr()
        self.assertTrue(result.is_proper())


class TestQuasiquoteDottedPair(SpecialsTestCase):
    """
    Quasi-quoting improper (dotted) lists.

    Note: `(a . ,x) at the reader level becomes the three-element proper list
    (quasiquote (a unquote x)) because the dotted-pair syntax
    `(X . (Y Z))` is identical to the list `(X Y Z)`.  Only atomic (non-pair)
    dotted tails produce genuine improper pairs.
    """

    async def test_dotted_constant_tail(self):
        """
        `(a . b) yields (a . b) as an improper pair
        """

        expr, mod = self.compile_expr("`(a . b)")
        result = await expr()
        self.assertEqual(symbol("a"), result[0])
        self.assertEqual(symbol("b"), result[1])
        self.assertFalse(result.is_proper())


    async def test_dotted_number_tail(self):
        """
        `(a . 42) yields (a . 42) as an improper pair (atom tail)
        """

        expr, mod = self.compile_expr("`(a . 42)")
        result = await expr()
        self.assertEqual(symbol("a"), result[0])
        self.assertEqual(42, result[1])
        self.assertFalse(result.is_proper())


    async def test_dotted_multi_element_constant_tail(self):
        """
        `(a b . c) yields the improper list (a b . c)
        """

        expr, mod = self.compile_expr("`(a b . c)")
        result = await expr()
        self.assertEqual(symbol("a"), result[0])
        self.assertEqual(symbol("b"), result[1][0])
        self.assertEqual(symbol("c"), result[1][1])
        self.assertFalse(result.is_proper())


    async def test_dotted_tail_unquote_pair(self):
        """
        `(,x . ,y) with x='foo and y=5 yields improper (foo . 5)
        """

        expr, mod = self.compile_expr("`(,x . ,y)", x="foo", y=5)
        result = await expr()
        self.assertEqual("foo", result[0])
        self.assertEqual(5, result[1])
        self.assertFalse(result.is_proper())


class TestQuasiquoteNested(SpecialsTestCase):
    """
    Nested quasi-quote: inner quasiquotes increment the nesting level so
    inner unquotes are not evaluated until the outer quasiquote is active
    """

    async def test_nested_quasiquote_inner_unquote_not_evaluated(self):
        """
        ``(a ,(+ 1 2)) at the outer level yields (quasiquote (a (unquote (+ 1 2))))
        as a literal data structure — the inner unquote is NOT evaluated
        """

        expr, mod = self.compile_expr("``(a ,(+ 1 2))")
        result = await expr()
        # result is (quasiquote (a (unquote (+ 1 2))))
        self.assertEqual(symbol("quasiquote"), result[0])
        inner = result[1][0]
        self.assertEqual(symbol("a"), inner[0])
        unquote_form = inner[1][0]
        self.assertEqual(symbol("unquote"), unquote_form[0])


    async def test_nested_quasiquote_level_tracking(self):
        """
        ``x yields (quasiquote x) as a literal pair
        """

        expr, mod = self.compile_expr("``x")
        result = await expr()
        self.assertEqual(symbol("quasiquote"), result[0])
        self.assertEqual(symbol("x"), result[1][0])


class TestQuasiquoteErrors(SpecialsTestCase):
    """
    Error cases: unquote and unquote-splicing outside quasiquote
    """

    async def test_unquote_outside_quasiquote_raises(self):
        """
        (unquote x) at top level raises SpecialError
        """

        self.assertRaises(SpecialError, self.compile_expr, "(unquote x)")


    async def test_unquote_splicing_outside_quasiquote_raises(self):
        """
        (unquote-splicing x) at top level raises SpecialError
        """

        self.assertRaises(
            SpecialError,
            self.compile_expr,
            "(unquote-splicing x)")


    async def test_unquote_splicing_outside_quasiquote_also_raises(self):
        """
        (unquote-splicing x) at top level raises SpecialError
        """

        self.assertRaises(
            SpecialError,
            self.compile_expr,
            "(unquote-splicing x)")


from koskari.compile import Compiler
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.reader import LayoutReader, source_str
from koskari.lib import Module, ModuleNamespace, keyword as _kw_type
from koskari.reader.layouts import apply_layout as _apply_layout_macro
from koskari.types import is_pair

_keyword_block = _kw_type(':block:')


def _make_block(head_args_tuple, body_tuple):
    """
    Build a :block: cons from Python tuples.

    head_args_tuple is a tuple of atoms (head, arg0, arg1, ...),
    body_tuple is a tuple of body forms.
    """

    head_args = cons(*head_args_tuple, nil)
    body = cons(*body_tuple, nil) if body_tuple else nil
    return cons(_keyword_block, head_args, body, nil)


class QuasiquoteBlockTestCase(unittest.IsolatedAsyncioTestCase):
    """
    Base for quasiquote-block tests.

    quasiquote-block rewrites to quasiquote on the raw :block:
    cons. When compiled and run, it produces a :block: cons with all unquote
    slots filled in — the same shape the reader uses for blocks, without an
    embedded layout wrapper in the datum. Macro expansion applies
    koskari.reader.layouts.apply_layout to such output; these tests mirror
    that for the second compile+run phase.
    """

    def make_module(self, **defns):
        """
        Create a module with specials and layout handlers registered.

        :param defns: name->value bindings
        :return: (module, mod_ns, compiler)
        """

        mod_ns = ModuleNamespace("test", defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)
        compiler = Compiler("<unittest>", module, filename=__file__)
        return module, mod_ns, compiler


    async def compile_cons_to_source(self, source, **defns):
        """
        Compile a cons source form and run it to obtain the filled :block:
        cons produced by quasiquote-block (via quasiquote).

        :param source: a cons to compile (typically quasiquote-block ...)
        :param defns: name->value bindings placed in the module namespace
        :return: (intermediate :block: cons, module, mod_ns, compiler)
        """

        module, mod_ns, compiler = self.make_module(**defns)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        intermediate = await expr()
        return intermediate, module, mod_ns, compiler


    async def compile_layout_src_to_source(self, src_str, **defns):
        """
        Read a layout-syntax source string, compile it, and run it to obtain
        the filled :block: cons produced by quasiquote-block.

        Uses a LayoutReader with all layout handlers registered so block syntax
        (e.g. `begin: ...) is correctly parsed.

        :param src_str: koskari layout source containing a quasiquote-block form
        :param defns: name->value bindings placed in the module namespace
        :return: (intermediate :block: cons, module, mod_ns, compiler)
        """

        module, mod_ns, compiler = self.make_module(**defns)
        layout_builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(layout_builtins)
        stream = source_str(src_str, __file__)
        source = reader.read(stream)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        intermediate = await expr()
        return intermediate, module, mod_ns, compiler


    async def run_source(self, intermediate, module, mod_ns, compiler):
        """
        Compile and run the second phase: expand :block: through layouts
        (as macro output does via apply_layout), then evaluate.

        :param intermediate: the :block: cons from the first phase
        :param module: the module with specials and layouts registered
        :param mod_ns: the module namespace
        :param compiler: the compiler to reuse
        :return: the final evaluated value
        """

        expanded = _apply_layout_macro(intermediate, vars(module))
        fragment2 = compiler.full_compile(expanded)
        expr2 = fragment2.as_expression(mod_ns)
        return await expr2()


class TestQuasiquoteBlockShape(QuasiquoteBlockTestCase):
    """
    Type-1 tests: verify the shape of the intermediate :block: cons produced
    by running compiled quasiquote-block code (unquote slots filled).
    """

    async def test_static_begin_block_shape(self):
        """
        Static block with no unquotes produces (:block: (begin) (42))
        """

        block = _make_block((symbol('begin'),), (42,))
        source = cons(symbol('quasiquote'), block, nil)
        intermediate, _, _, _ = await self.compile_cons_to_source(source)

        self.assertTrue(is_pair(intermediate))
        kw, block_tail = intermediate
        self.assertIs(kw, _keyword_block)
        self.assertTrue(is_pair(block_tail))
        head_args, body_tail = block_tail
        self.assertTrue(is_pair(head_args))
        self.assertEqual(head_args[0], symbol('begin'))
        body_exprs, rest = body_tail
        self.assertIs(rest, nil)
        self.assertTrue(is_pair(body_exprs))
        self.assertEqual(body_exprs[0], 42)


    async def test_unquote_slot_filled(self):
        """
        A ,x unquote site is filled with the runtime value of x in the
        intermediate source cons
        """

        unquote_x = cons(symbol('unquote'), symbol('x'), nil)
        block = _make_block((symbol('begin'),), (unquote_x,))
        source = cons(symbol('quasiquote'), block, nil)
        intermediate, _, _, _ = await self.compile_cons_to_source(source, x=99)

        self.assertTrue(is_pair(intermediate))
        block_kw, block_tail = intermediate
        self.assertIs(block_kw, _keyword_block)
        _, body_tail = block_tail
        body_exprs, _ = body_tail
        first_body_elem, _ = body_exprs
        self.assertEqual(first_body_elem, 99)


    async def test_unquote_splicing_slot_filled(self):
        """
        A ,@forms unquote-splicing site is spliced into the body list in the
        intermediate source cons
        """

        splice = cons(symbol('unquote-splicing'), symbol('forms'), nil)
        block = _make_block((symbol('begin'),), (splice,))
        source = cons(symbol('quasiquote'), block, nil)
        forms = cons(10, 20, nil)
        intermediate, _, _, _ = await self.compile_cons_to_source(source, forms=forms)

        self.assertTrue(is_pair(intermediate))
        block_kw, block_tail = intermediate
        self.assertIs(block_kw, _keyword_block)
        _, body_tail = block_tail
        body_exprs, _ = body_tail
        first_elem, rest_elems = body_exprs
        second_elem, _ = rest_elems
        self.assertEqual(first_elem, 10)
        self.assertEqual(second_elem, 20)


class TestQuasiquoteBlockTwoPhase(QuasiquoteBlockTestCase):
    """
    Type-2 tests: two-phase compile of quasiquote-block forms.

    Phase 1 compiles and runs the quasiquote-block, producing a :block: cons
    with slots filled. Phase 2 runs koskari.reader.layouts.apply_layout,
    then compiles and runs to obtain the final value.
    """

    async def test_static_begin_block(self):
        """
        Static block with no unquotes; two-phase result is 42
        """

        block = _make_block((symbol('begin'),), (42,))
        source = cons(symbol('quasiquote'), block, nil)
        intermediate, module, mod_ns, compiler = await self.compile_cons_to_source(source)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 42)


    async def test_unquote_in_body(self):
        """
        ,x unquote in body; with x=99 two-phase result is 99
        """

        unquote_x = cons(symbol('unquote'), symbol('x'), nil)
        block = _make_block((symbol('begin'),), (unquote_x,))
        source = cons(symbol('quasiquote'), block, nil)
        intermediate, module, mod_ns, compiler = await self.compile_cons_to_source(
            source, x=99)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 99)


    async def test_unquote_splicing_in_body(self):
        """
        ,@forms splice in body; with forms=(1 2 3) two-phase result is 3
        """

        splice = cons(symbol('unquote-splicing'), symbol('forms'), nil)
        block = _make_block((symbol('begin'),), (splice,))
        source = cons(symbol('quasiquote'), block, nil)
        forms = cons(1, 2, 3, nil)
        intermediate, module, mod_ns, compiler = await self.compile_cons_to_source(
            source, forms=forms)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 3)


    async def test_if_else_peers_true_branch(self):
        """
        begin block with if/else peer siblings; with cond=True result is 1
        """

        unquote_cond = cons(symbol('unquote'), symbol('cond'), nil)
        if_block = _make_block((symbol('if'), unquote_cond), (1,))
        else_block = _make_block((symbol('else'),), (2,))
        outer = _make_block((symbol('begin'),), (if_block, else_block))
        source = cons(symbol('quasiquote'), outer, nil)
        intermediate, module, mod_ns, compiler = await self.compile_cons_to_source(
            source, cond=True)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 1)


    async def test_if_else_peers_false_branch(self):
        """
        begin block with if/else peer siblings; with cond=False result is 2
        """

        unquote_cond = cons(symbol('unquote'), symbol('cond'), nil)
        if_block = _make_block((symbol('if'), unquote_cond), (1,))
        else_block = _make_block((symbol('else'),), (2,))
        outer = _make_block((symbol('begin'),), (if_block, else_block))
        source = cons(symbol('quasiquote'), outer, nil)
        intermediate, module, mod_ns, compiler = await self.compile_cons_to_source(
            source, cond=False)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 2)


class TestQuasiquoteBlockFullStack(QuasiquoteBlockTestCase):
    """
    Full-stack integration tests: read layout source via the default reader,
    compile, run (producing the filled :block: cons), then apply_layout plus
    compile+run again to obtain the final value.
    """

    async def test_static_begin_block(self):
        """
        `begin: 42 — two-phase result is 42
        """

        src = """
        `begin:
          42
        """
        intermediate, module, mod_ns, compiler = await self.compile_layout_src_to_source(src)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 42)


    async def test_unquote_body(self):
        """
        `begin: ,x — with x=99 two-phase result is 99
        """

        src = """
        `begin:
          ,x
        """
        intermediate, module, mod_ns, compiler = await self.compile_layout_src_to_source(
            src, x=99)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 99)


    async def test_unquote_splicing_body(self):
        """
        `begin: ,@forms — with forms=(1 2 3) two-phase result is 3
        """

        src = """
        `begin:
          ,@forms
        """
        forms = cons(1, 2, 3, nil)
        intermediate, module, mod_ns, compiler = await self.compile_layout_src_to_source(
            src, forms=forms)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 3)


    async def test_if_else_unquote_true(self):
        """
        `begin: if ,cond: ,yes else: ,no — with cond=True two-phase result is 1
        """

        src = """
        `begin:
          if ,cond:
            ,yes
          else:
            ,no
        """
        intermediate, module, mod_ns, compiler = await self.compile_layout_src_to_source(
            src, cond=True, yes=1, no=2)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 1)


    async def test_if_else_unquote_false(self):
        """
        `begin: if ,cond: ,yes else: ,no — with cond=False two-phase result is 2
        """

        src = """
        `begin:
          if ,cond:
            ,yes
          else:
            ,no
        """
        intermediate, module, mod_ns, compiler = await self.compile_layout_src_to_source(
            src, cond=False, yes=1, no=2)
        result = await self.run_source(intermediate, module, mod_ns, compiler)
        self.assertEqual(result, 2)


# The end.
