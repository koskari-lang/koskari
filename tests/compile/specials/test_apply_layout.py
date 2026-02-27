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
Tests for koskari.compile.specials.apply_layout (compile/apply-layout).

Verifies depth-first :block: expansion through the layout handler system,
peer consumption, layout/block fallback, error cases, nested body blocks,
and multi-block begin wrapping.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""

import unittest

from koskari.compile import Compiler
from koskari.compile.specials import SPECIALS
from koskari.exceptions import SpecialError
from koskari.lib import cons, nil
from koskari.reader import default_reader, source_str
from koskari.reader.layouts import LAYOUTS
from koskari.types import keyword, symbol, Module, ModuleNamespace

from . import _register_specials


_keyword_block = keyword(':block:')

_symbol_begin = symbol('compile/begin')
_symbol_cond = symbol('compile/cond')
_symbol_else = keyword('else')


def _make_block(head_args_tuple, body_tuple):
    """
    Build a :block: cons from Python tuples.

    head_args_tuple is a tuple of atoms (head, arg0, arg1, ...),
    body_tuple is a tuple of body forms.
    """

    head_args = cons(*head_args_tuple, nil)
    body = cons(*body_tuple, nil) if body_tuple else nil
    return cons(_keyword_block, head_args, body, nil)


def _register_layouts(ns):
    for sym, handler in LAYOUTS.items():
        ns.store(str(sym), handler)


class ApplyLayoutTestCase(unittest.TestCase):
    """
    Base for compile/apply-layout tests
    """

    def compile_expr(self, source, **defns):
        mod_ns = ModuleNamespace("test", defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, SPECIALS)
        _register_layouts(ns)
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        return expr, module


class TestApplyLayoutBegin(ApplyLayoutTestCase):
    """
    compile/apply-layout on a begin block
    """

    def test_begin_block_runs_body(self):
        """
        (:block: (begin) (42)) expands to (compile/begin 42) and evaluates to 42
        """

        block = _make_block((symbol('begin'),), (42,))
        source = cons(symbol('compile/apply-layout'), block, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 42)


    def test_begin_block_last_value(self):
        """
        (:block: (begin) (1 2 3)) evaluates to 3 (last expression)
        """

        block = _make_block(
            (symbol('begin'),),
            (1, 2, 3))
        source = cons(symbol('compile/apply-layout'), block, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 3)


class TestApplyLayoutIf(ApplyLayoutTestCase):
    """
    compile/apply-layout on if blocks, with and without peers
    """

    def test_if_block_no_peers_true(self):
        """
        (:block: (if True) (99)) with no peer evaluates to 99 when condition is True
        """

        block = _make_block(
            (symbol('if'), True),
            (99,))
        source = cons(symbol('compile/apply-layout'), block, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 99)


    def test_if_block_no_peers_false(self):
        """
        (:block: (if False) (99)) with no peer evaluates to None when condition is False
        """

        block = _make_block(
            (symbol('if'), False),
            (99,))
        source = cons(symbol('compile/apply-layout'), block, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertIsNone(result)


    def test_if_else_peer_taken(self):
        """
        if True block with else peer evaluates the if branch
        """

        if_block = _make_block(
            (symbol('if'), True),
            (1,))
        else_block = _make_block(
            (symbol('else'),),
            (2,))
        source = cons(
            symbol('compile/apply-layout'),
            if_block, else_block, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 1)


    def test_if_else_peer_fallback(self):
        """
        if False block with else peer evaluates the else branch
        """

        if_block = _make_block(
            (symbol('if'), False),
            (1,))
        else_block = _make_block(
            (symbol('else'),),
            (2,))
        source = cons(
            symbol('compile/apply-layout'),
            if_block, else_block, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 2)


    def test_else_not_consumed_without_if(self):
        """
        An else block not preceded by an if is not consumed as a peer;
        it stands alone and falls back to layout/block, producing an
        (else (compile/begin 42)) call form. Since else is a callable
        in scope this evaluates normally.
        """

        else_block = _make_block(
            (symbol('else'),),
            (42,))
        source = cons(symbol('compile/apply-layout'), else_block, nil)
        called_with = []
        else_fn = lambda body: called_with.append(body) or 99
        expr, mod = self.compile_expr(source, **{'else': else_fn})
        result = expr().run()
        self.assertEqual(result, 99)


class TestApplyLayoutFallback(ApplyLayoutTestCase):
    """
    compile/apply-layout falls back to layout/block for unknown heads
    """

    def test_unknown_head_uses_layout_block(self):
        """
        (:block: (myfn arg) (body)) with no layout/myfn expands via layout/block
        to (myfn arg (compile/begin body))
        """

        block = _make_block(
            (symbol('myfn'), symbol('arg')),
            (symbol('body'),))
        source = cons(symbol('compile/apply-layout'), block, nil)
        expr, mod = self.compile_expr(
            source,
            myfn=lambda arg, body: (arg, body),
            arg=10,
            body=20)
        result = expr().run()
        self.assertEqual(result[0], 10)


class TestApplyLayoutNested(ApplyLayoutTestCase):
    """
    Nested :block: elements in a body are recursively expanded
    """

    def test_nested_begin_in_begin(self):
        """
        (:block: (begin) ((:block: (begin) (42)))) recursively expands the inner block
        """

        inner = _make_block((symbol('begin'),), (42,))
        outer = _make_block((symbol('begin'),), (inner,))
        source = cons(symbol('compile/apply-layout'), outer, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 42)


    def test_nested_if_in_begin(self):
        """
        (:block: (begin) ((:block: (if True) (7)))) expands nested if inside begin
        """

        inner = _make_block((symbol('if'), True), (7,))
        outer = _make_block((symbol('begin'),), (inner,))
        source = cons(symbol('compile/apply-layout'), outer, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 7)


class TestApplyLayoutMultipleBlocks(ApplyLayoutTestCase):
    """
    Multiple top-level block arguments are wrapped in compile/begin
    """

    def test_two_begin_blocks_wrapped(self):
        """
        Two begin blocks produce a begin-wrapped result; last value wins
        """

        block1 = _make_block((symbol('begin'),), (1,))
        block2 = _make_block((symbol('begin'),), (2,))
        source = cons(symbol('compile/apply-layout'), block1, block2, nil)
        expr, mod = self.compile_expr(source)
        result = expr().run()
        self.assertEqual(result, 2)


class TestApplyLayoutErrors(ApplyLayoutTestCase):
    """
    Error cases for compile/apply-layout
    """

    def test_no_args_raises(self):
        """
        (compile/apply-layout) with no arguments raises SpecialError
        """

        source = cons(symbol('compile/apply-layout'), nil)
        self.assertRaises(SpecialError, self.compile_expr, source)


    def test_non_block_arg_raises(self):
        """
        A non-:block: argument raises SpecialError
        """

        source = cons(symbol('compile/apply-layout'), symbol('foo'), nil)
        self.assertRaises(SpecialError, self.compile_expr, source)


    def test_plain_list_arg_raises(self):
        """
        A plain cons list (not tagged with :block:) raises SpecialError
        """

        plain = cons(symbol('begin'), symbol('foo'), nil)
        source = cons(symbol('compile/apply-layout'), plain, nil)
        self.assertRaises(SpecialError, self.compile_expr, source)


# The end.
