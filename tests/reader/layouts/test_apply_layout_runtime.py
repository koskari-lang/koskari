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
tests.reader.layouts.test_apply_layout_runtime

Unit and integration tests for koskari.reader.layouts.apply_layout and
apply_layouts — runtime expansion of :#block: cons structures through layout
handlers, mirroring scenarios formerly covered via compile/apply-layout.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile import Compiler
from koskari.types import is_pair
from koskari.lib import Module, ModuleNamespace, cons, keyword, nil, symbol
from koskari.reader.layouts import apply_layout, apply_layouts
from koskari.types import unpack
from tests.compile.specials import _register_specials


pytestmark = pytest.mark.phase_infra


_keyword_block = keyword('#block')
_keyword_splice = keyword('#splice')
_keyword_line = keyword('#line')
_symbol_compile_begin = symbol('begin')


def _make_block(head_args_tuple, body_tuple):
    """
    Build a :#block: cons from Python tuples.

    head_args_tuple is a tuple of atoms (head, arg0, arg1, ...),
    body_tuple is a tuple of body forms, each becoming a one-element
    :#line: cons, or nested blocks and prebuilt lines taken as they are.
    """

    head_args = cons(*head_args_tuple, nil)
    entries = [form if _is_entry(form) else cons(_keyword_line, form, nil)
               for form in body_tuple]
    return cons(_keyword_block, head_args, *entries, nil)


def _is_entry(form):
    """
    A nested block, or a line already built, goes in as it is
    """

    return (is_pair(form)
            and (form.car is _keyword_block
                 or form.car == _keyword_line
                 or form.car == _keyword_splice))


class ApplyLayoutRuntimeTestCase(unittest.IsolatedAsyncioTestCase):
    """
    Base helpers for apply_layout / apply_layouts runtime tests
    """

    def make_ns_and_compiler(self, **defns):
        """
        Module namespace with specials registered for compilation.

        :param defns: extra bindings merged into the namespace
        :return: (module, mod_ns, compiler, ns_mapping)
        """

        mod_ns = ModuleNamespace("test", defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)
        compiler = Compiler("<unittest>", module, filename=__file__)
        return module, mod_ns, compiler, ns


    async def eval_expanded_form(self, expanded, mod_ns, compiler):
        """
        Compile and evaluate an expanded source form.

        :param expanded: cons structure suitable for Compiler.full_compile
        :param mod_ns: module namespace for Expression closure
        :param compiler: Compiler tied to the module
        :return: evaluated result
        """

        fragment = compiler.full_compile(expanded)
        expr = fragment.as_expression(mod_ns)
        return await expr()


    async def eval_apply_layouts_spine_as_begin(self, spine, mod_ns, compiler, ns):
        """
        Expand a spine of :#block: nodes and evaluate the results in order.

        apply_layouts yields a proper list of sibling source forms; the
        compiler requires a single expression root, so the list is wrapped
        in compile/begin.

        :param spine: proper list of :#block: cons nodes
        :param mod_ns: module namespace for Expression closure
        :param compiler: Compiler tied to the module
        :param ns: layout handler lookup namespace
        :return: evaluated result
        """

        expanded = apply_layouts(spine, ns)
        wrapped = cons(_symbol_compile_begin, expanded)
        return await self.eval_expanded_form(wrapped, mod_ns, compiler)


class TestApplyLayoutPassthrough(ApplyLayoutRuntimeTestCase):
    """
    apply_layout and apply_layouts behavior without compilation
    """

    def test_apply_layout_nil(self):
        """
        apply_layout(nil, ns) yields nil
        """

        _, _, _, ns = self.make_ns_and_compiler()
        self.assertIs(apply_layout(nil, ns), nil)


    def test_apply_layout_non_block_returns_same_object(self):
        """
        Non-:#block: pairs are returned unchanged
        """

        _, _, _, ns = self.make_ns_and_compiler()
        plain = cons(symbol('begin'), symbol('foo'), nil)
        self.assertIs(apply_layout(plain, ns), plain)


    def test_apply_layouts_empty_spine(self):
        """
        apply_layouts(nil, ns) yields nil
        """

        _, _, _, ns = self.make_ns_and_compiler()
        self.assertIs(apply_layouts(nil, ns), nil)


    def test_apply_layouts_passes_through_non_block_atom(self):
        """
        A non-:#block: form on the spine is copied through unchanged
        """

        _, _, _, ns = self.make_ns_and_compiler()
        spine = cons(symbol('foo'), nil)
        got = apply_layouts(spine, ns)
        self.assertEqual(list(unpack(got)), [symbol('foo')])


    def test_missing_layout_block_raises(self):
        """
        Expanding a :#block: requires layout/block in the namespace
        """

        block = _make_block((symbol('begin'),), (42,))
        with self.assertRaises(RuntimeError):
            apply_layout(block, {})


class TestApplyLayoutBeginRuntime(ApplyLayoutRuntimeTestCase):
    """
    apply_layout on begin blocks, then compile and run
    """

    async def test_begin_block_runs_body(self):
        """
        (:#block: (begin) (42)) expands and evaluates to 42
        """

        block = _make_block((symbol('begin'),), (42,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        expanded = apply_layout(block, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result, 42)


    async def test_begin_block_last_value(self):
        """
        (:#block: (begin) (1 2 3)) evaluates to 3 (last expression)
        """

        block = _make_block(
            (symbol('begin'),),
            (1, 2, 3))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        expanded = apply_layout(block, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result, 3)


class TestApplyLayoutIfRuntime(ApplyLayoutRuntimeTestCase):
    """
    apply_layouts with if peers on the argument spine
    """

    async def test_if_block_no_peers_true(self):
        """
        (:#block: (if True) (99)) with no peer evaluates to 99 when condition is True
        """

        block = _make_block(
            (symbol('if'), True),
            (99,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        expanded = apply_layout(block, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result, 99)


    async def test_if_block_no_peers_false(self):
        """
        (:#block: (if False) (99)) with no peer evaluates to None when False
        """

        block = _make_block(
            (symbol('if'), False),
            (99,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        expanded = apply_layout(block, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertIsNone(result)


    async def test_if_else_peer_taken(self):
        """
        if True block with else peer evaluates the if branch
        """

        if_block = _make_block(
            (symbol('if'), True),
            (1,))
        else_block = _make_block(
            (symbol('else'),),
            (2,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        spine = cons(if_block, else_block, nil)
        result = await self.eval_apply_layouts_spine_as_begin(
            spine, mod_ns, compiler, ns)
        self.assertEqual(result, 1)


    async def test_if_else_peer_fallback(self):
        """
        if False block with else peer evaluates the else branch
        """

        if_block = _make_block(
            (symbol('if'), False),
            (1,))
        else_block = _make_block(
            (symbol('else'),),
            (2,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        spine = cons(if_block, else_block, nil)
        result = await self.eval_apply_layouts_spine_as_begin(
            spine, mod_ns, compiler, ns)
        self.assertEqual(result, 2)


    async def test_else_not_consumed_without_if(self):
        """
        An else block alone is not paired as an if peer; layout/block applies
        """

        else_block = _make_block(
            (symbol('else'),),
            (42,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        called_with = []

        def else_fn(body):
            called_with.append(body)
            return 99

        ns['else'] = else_fn
        expanded = apply_layout(else_block, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result, 99)


class TestApplyLayoutFallbackRuntime(ApplyLayoutRuntimeTestCase):
    """
    Unknown block heads fall back to layout/block
    """

    async def test_unknown_head_uses_layout_block(self):
        """
        (:#block: (myfn arg) (body)) expands via layout/block to myfn(arg, body)
        """

        block = _make_block(
            (symbol('myfn'), symbol('arg')),
            (symbol('body'),))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler(
            myfn=lambda arg, body: (arg, body),
            arg=10,
            body=20)
        expanded = apply_layout(block, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result[0], 10)


class TestApplyLayoutNestedRuntime(ApplyLayoutRuntimeTestCase):
    """
    Nested :#block: elements in body lists expand depth-first
    """

    async def test_nested_begin_in_begin(self):
        """
        Inner (:#block: (begin) (42)) expands inside outer begin
        """

        inner = _make_block((symbol('begin'),), (42,))
        outer = _make_block((symbol('begin'),), (inner,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        expanded = apply_layout(outer, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result, 42)


    async def test_nested_if_in_begin(self):
        """
        Nested if block inside begin expands correctly
        """

        inner = _make_block((symbol('if'), True), (7,))
        outer = _make_block((symbol('begin'),), (inner,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        expanded = apply_layout(outer, ns)
        result = await self.eval_expanded_form(expanded, mod_ns, compiler)
        self.assertEqual(result, 7)


class TestApplyLayoutsMultipleBlocksRuntime(ApplyLayoutRuntimeTestCase):
    """
    Multiple top-level blocks on one spine compile like sequential expressions
    """

    async def test_two_begin_blocks_sequence(self):
        """
        Two begin blocks on one spine yield last expression value
        """

        block1 = _make_block((symbol('begin'),), (1,))
        block2 = _make_block((symbol('begin'),), (2,))
        _, mod_ns, compiler, ns = self.make_ns_and_compiler()
        spine = cons(block1, block2, nil)
        result = await self.eval_apply_layouts_spine_as_begin(
            spine, mod_ns, compiler, ns)
        self.assertEqual(result, 2)


# The end.
