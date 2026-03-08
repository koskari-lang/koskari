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
Unit tests for koskari.dis (disassembler).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import io
import unittest

from koskari.lib import Code, OpCode
from koskari.types import NamespaceSpec
from koskari.dis import (
    dis,
    _get_code,
    _get_instruction_lines,
    _format_constant,
    _build_ns_const_map,
    _format_nspec_inline,
)


def _make_truthy_code():
    """
    Code: VAR YUS, jump-if-true to 4, else CONST NO, JUMP 5, CONST YES, RETURN
    """
    instr = (
        [OpCode.KOS_OP_VAR, "YUS"],
        [OpCode.KOS_OP_JUMP_TRUE, "truthy"],
        [OpCode.KOS_OP_CONST, "NO"],
        [OpCode.KOS_OP_JUMP, "done"],
        [OpCode.KOS_OP_LABEL, "truthy"],
        [OpCode.KOS_OP_CONST, "YES"],
        [OpCode.KOS_OP_LABEL, "done"],
        [OpCode.KOS_OP_RETURN, 1],
    )
    nspec = NamespaceSpec(None, (), ())
    return Code(nspec, instr, "truthy_yus", "something.ki")


def _make_push_ns_code():
    """
    Code that emits CONST <nspec> then PUSH_NS 0 then POP_NS, so we
    can verify the namespace annotation on PUSH_NS.
    """
    child_nspec = NamespaceSpec('let', ('x', 'y'), ('outer',))
    instr = (
        [OpCode.KOS_OP_CONST, child_nspec],
        [OpCode.KOS_OP_PUSH_NS, 0],
        [OpCode.KOS_OP_CONST, None],
        [OpCode.KOS_OP_POP_NS, 1],
        [OpCode.KOS_OP_RETURN, 1],
    )
    outer_nspec = NamespaceSpec('outer', ('outer',), ())
    return Code(outer_nspec, instr, "push_ns_fn", "file.ki")


def _make_fast_code():
    """
    Code that uses FAST (local slot 0) and FAST (closure slot 2, which is
    closure index 0 when there are 2 locals).
    """
    local_syms = ('a', 'b')
    closure_syms = ('outer',)
    nspec = NamespaceSpec('fn', local_syms, closure_syms)
    instr = (
        [OpCode.KOS_OP_FAST, 0],
        [OpCode.KOS_OP_FAST, 2],
        [OpCode.KOS_OP_RETURN, 1],
    )
    return Code(nspec, instr, "fast_fn", "file.ki")


class TestGetCode(unittest.IsolatedAsyncioTestCase):

    async def test_code_passthrough(self):
        code = _make_truthy_code()
        self.assertIs(_get_code(code), code)

    async def test_non_code_returns_none(self):
        self.assertIs(_get_code(42), None)
        self.assertIs(_get_code(type('NoCode', (), {})()), None)

    async def test_object_with_code_attr_not_accepted(self):
        """
        Only Code is accepted; .code attribute is not resolved
        """
        code = _make_truthy_code()
        wrapper = type('Wrapper', (), {'code': code})()
        self.assertIs(_get_code(wrapper), None)
        with self.assertRaises(TypeError):
            dis(wrapper)

    async def test_non_code_raises_from_dis(self):
        from koskari.dis import dis
        with self.assertRaises(TypeError):
            dis(42)


class TestDisOutput(unittest.IsolatedAsyncioTestCase):

    async def test_dis_truthy_yus(self):
        code = _make_truthy_code()
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn('KosCode', text)
        self.assertIn('truthy_yus', text)
        self.assertIn('Constant pool (3):', text)
        self.assertIn('YUS', text)
        self.assertIn('NO', text)
        self.assertIn('YES', text)
        self.assertIn('Vars (local_syms', text)
        self.assertIn('Closure refs', text)
        self.assertIn('Instructions (6):', text)
        self.assertIn('KOS_OP_VAR', text)
        self.assertIn('0 (', text)
        self.assertIn('KOS_OP_JUMP_TRUE', text)
        self.assertIn('(-> 4)', text)
        self.assertIn('KOS_OP_RETURN', text)

    async def test_dis_with_locals_and_closures(self):
        local_syms = ('a', 'b')
        closure_syms = ('outer',)
        nspec = NamespaceSpec(None, local_syms, closure_syms)
        instr = (
            [OpCode.KOS_OP_FAST_REF, 0],
            [OpCode.KOS_OP_RETURN, 1],
        )
        code = Code(nspec, instr, "fn", "file.ki")
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn('Vars (local_syms, 2):', text)
        self.assertIn("'a'", text)
        self.assertIn("'b'", text)
        self.assertIn('Closure refs (1):', text)
        self.assertIn("'outer'", text)

    async def test_nspec_in_constant_pool_is_expanded(self):
        """
        NamespaceSpec entries in the constant pool are shown with ns# label
        and their locals/closures lines rather than a bare repr
        """
        code = _make_push_ns_code()
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn('ns#0:', text)
        self.assertIn("NamespaceSpec 'let'", text)
        self.assertIn("locals:", text)
        self.assertIn("closures:", text)
        self.assertIn("('x', 'y')", text)
        self.assertIn("('outer',)", text)

    async def test_push_ns_annotated_with_ns_id(self):
        """
        PUSH_NS instructions show the ns# label and namespace name
        """
        code = _make_push_ns_code()
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn('KOS_OP_PUSH_NS', text)
        push_ns_line = next(
            line for line in text.splitlines()
            if 'KOS_OP_PUSH_NS' in line
        )
        self.assertIn('ns#0', push_ns_line)
        self.assertIn("'let'", push_ns_line)

    async def test_const_nspec_shows_ns_inline(self):
        """
        CONST instructions that load a NamespaceSpec show the ns# inline label
        """
        code = _make_push_ns_code()
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        const_ns_line = next(
            line for line in text.splitlines()
            if 'KOS_OP_CONST' in line and 'ns#' in line
        )
        self.assertIn('ns#0', const_ns_line)
        self.assertIn("'let'", const_ns_line)

    async def test_fast_local_label(self):
        """
        FAST accessing a local slot shows 'local' prefix
        """
        code = _make_fast_code()
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        lines = [l for l in text.splitlines() if 'KOS_OP_FAST' in l]
        self.assertTrue(any("local 'a'" in l for l in lines))

    async def test_fast_closure_label(self):
        """
        FAST accessing a closure slot shows 'closure' prefix
        """
        code = _make_fast_code()
        buf = io.StringIO()
        dis(code, file=buf)
        text = buf.getvalue()
        lines = [l for l in text.splitlines() if 'KOS_OP_FAST' in l]
        self.assertTrue(any("closure 'outer'" in l for l in lines))


class TestGetInstructionLines(unittest.IsolatedAsyncioTestCase):

    async def test_instruction_lines_match_oparg(self):
        code = _make_truthy_code()
        lines = list(_get_instruction_lines(code))
        self.assertEqual(len(lines), 6)
        self.assertEqual(lines[0][0], 0)
        self.assertEqual(lines[0][1], 'KOS_OP_VAR')
        self.assertIn('0 (', lines[0][2])
        self.assertIn('YUS', lines[0][2])
        self.assertEqual(lines[1][1], 'KOS_OP_JUMP_TRUE')
        self.assertIn('(-> 4)', lines[1][2])

    async def test_push_ns_in_instruction_lines(self):
        """
        _get_instruction_lines includes ns# annotation for PUSH_NS
        """
        code = _make_push_ns_code()
        lines = list(_get_instruction_lines(code))
        push_ns = next(
            (idx, name, arg) for idx, name, arg in lines
            if name == 'KOS_OP_PUSH_NS'
        )
        self.assertIn('ns#0', push_ns[2])
        self.assertIn("'let'", push_ns[2])

    async def test_fast_local_in_instruction_lines(self):
        """
        _get_instruction_lines shows 'local' for fast local slots
        """
        code = _make_fast_code()
        lines = list(_get_instruction_lines(code))
        fast_lines = [(n, a) for _, n, a in lines if n == 'KOS_OP_FAST']
        self.assertTrue(any("local 'a'" in a for _, a in fast_lines))

    async def test_fast_closure_in_instruction_lines(self):
        """
        _get_instruction_lines shows 'closure' for fast closure slots
        """
        code = _make_fast_code()
        lines = list(_get_instruction_lines(code))
        fast_lines = [(n, a) for _, n, a in lines if n == 'KOS_OP_FAST']
        self.assertTrue(any("closure 'outer'" in a for _, a in fast_lines))


class TestFormatConstant(unittest.IsolatedAsyncioTestCase):

    async def test_short_repr_unchanged(self):
        self.assertEqual(_format_constant(42), '42')
        self.assertEqual(_format_constant('x'), "'x'")

    async def test_long_repr_truncated(self):
        long_str = 'x' * 70
        out = _format_constant(long_str)
        self.assertEqual(len(out), 60)
        self.assertTrue(out.endswith('...'))


class TestBuildNsConstMap(unittest.IsolatedAsyncioTestCase):

    async def test_push_ns_maps_to_preceding_nspec_const(self):
        code = _make_push_ns_code()
        ns_map = _build_ns_const_map(code)
        # instruction index 1 is PUSH_NS; it follows CONST at index 0
        self.assertIn(1, ns_map)
        self.assertEqual(ns_map[1], 0)

    async def test_no_push_ns_gives_empty_map(self):
        code = _make_truthy_code()
        ns_map = _build_ns_const_map(code)
        self.assertEqual(ns_map, {})

    async def test_only_nspec_const_maps_to_push_ns(self):
        """
        Only CONST instructions that load a NamespaceSpec contribute to
        the ns_const_map; other values do not produce a mapping entry
        """
        code = _make_push_ns_code()
        ns_map = _build_ns_const_map(code)
        # Only index 1 (PUSH_NS) should appear, not any CONST of a plain value
        cpool = code.constants_pool
        for i, cpool_idx in ns_map.items():
            self.assertIsInstance(cpool[cpool_idx], NamespaceSpec)


class TestFormatNspecInline(unittest.IsolatedAsyncioTestCase):

    async def test_named_nspec(self):
        nspec = NamespaceSpec('let', ('a',), ())
        result = _format_nspec_inline(5, nspec)
        self.assertEqual(result, "ns#5 'let'")

    async def test_none_named_nspec(self):
        nspec = NamespaceSpec(None, (), ())
        result = _format_nspec_inline(0, nspec)
        self.assertEqual(result, 'ns#0 None')


# The end.
