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
Unit tests for koskari.dis_ng (koskari-native disassembler experiment).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: gpt-5.3-codex-low via Cursor
"""

import io
import unittest

import pytest

from koskari.dis_ng import (_build_ns_const_map, _format_constant,
                            _format_nspec_inline, _get_code,
                            _get_instruction_lines, dis)
from koskari.lib import Code, FormalsSpec, NamespaceSpec, OpCode
from tests import make_callable


pytestmark = pytest.mark.phase_infra


def _make_truthy_code():
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
    child_nspec = NamespaceSpec("let", ("x", "y"), ("outer",))
    instr = (
        [OpCode.KOS_OP_CONST, child_nspec],
        [OpCode.KOS_OP_PUSH_NS, 0],
        [OpCode.KOS_OP_CONST, None],
        [OpCode.KOS_OP_POP_NS, 1],
        [OpCode.KOS_OP_RETURN, 1],
    )
    outer_nspec = NamespaceSpec("outer", ("outer",), ())
    return Code(outer_nspec, instr, "push_ns_fn", "file.ki")


def _make_fast_code():
    local_syms = ("a", "b")
    closure_syms = ("outer",)
    nspec = NamespaceSpec("fn", local_syms, closure_syms)
    instr = (
        [OpCode.KOS_OP_FAST, 0],
        [OpCode.KOS_OP_FAST, 2],
        [OpCode.KOS_OP_RETURN, 1],
    )
    return Code(nspec, instr, "fast_fn", "file.ki")


def _make_function():
    code = _make_truthy_code()
    nspec = NamespaceSpec(None, ("outer",), ())
    base_ns = nspec.get_namespace(None)
    base_ns.store("outer", 99)
    return make_callable(code, base_ns, FormalsSpec(("x",), None, ("y",)))


class TestDisNgGetCode(unittest.IsolatedAsyncioTestCase):

    async def test_code_passthrough(self):
        code = _make_truthy_code()
        self.assertIs(await _get_code(code), code)

    async def test_non_code_returns_none(self):
        self.assertIs(await _get_code(42), None)


class TestDisNgOutput(unittest.IsolatedAsyncioTestCase):

    async def test_dis_truthy_yus(self):
        code = _make_truthy_code()
        buf = io.StringIO()
        await dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("KosCode", text)
        self.assertIn("truthy_yus", text)
        self.assertIn("Constant pool (3):", text)
        self.assertIn("Instructions (6):", text)
        self.assertIn("KOS_OP_VAR", text)
        self.assertIn("KOS_OP_JUMP_TRUE", text)
        self.assertIn("(-> 4)", text)

    async def test_dis_function_leading_section(self):
        fn = _make_function()
        buf = io.StringIO()
        await dis(fn, file=buf)
        text = buf.getvalue()
        self.assertIn("Function instance:", text)
        self.assertIn("formals:", text)
        self.assertIn("base_ns (closures):", text)
        self.assertIn("KosCode", text)

    async def test_push_ns_annotation(self):
        code = _make_push_ns_code()
        buf = io.StringIO()
        await dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("KOS_OP_PUSH_NS", text)
        self.assertIn("ns#0", text)
        self.assertIn("'let'", text)

    async def test_fast_slot_labels(self):
        code = _make_fast_code()
        buf = io.StringIO()
        await dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("local 'a'", text)
        self.assertIn("closure 'outer'", text)


class TestDisNgHelpers(unittest.IsolatedAsyncioTestCase):

    async def test_instruction_lines(self):
        code = _make_truthy_code()
        lines = [line async for line in _get_instruction_lines(code)]
        self.assertEqual(len(lines), 6)
        self.assertEqual(lines[0][1], "KOS_OP_VAR")
        self.assertIn("YUS", lines[0][2])

    async def test_nspec_inline(self):
        nspec = NamespaceSpec("let", ("a",), ())
        self.assertEqual(await _format_nspec_inline(5, nspec), "ns#5 'let'")

    async def test_format_constant(self):
        self.assertEqual(await _format_constant(42), "42")
        long_str = "x" * 70
        out = await _format_constant(long_str)
        self.assertEqual(len(out), 60)
        self.assertTrue(out.endswith("..."))

    async def test_build_ns_const_map(self):
        code = _make_push_ns_code()
        ns_map = await _build_ns_const_map(code)
        self.assertIn(1, ns_map)
        self.assertEqual(ns_map[1], 0)

    async def test_dis_non_code_raises(self):
        with self.assertRaises(TypeError):
            await dis("not code")


# The end.
