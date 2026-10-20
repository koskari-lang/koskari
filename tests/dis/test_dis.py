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
Unit tests for koskari.dis, the pure-Koskari disassembler.

Because koskari.dis is written in Koskari, it needs the full stack
(reader, compiler, importer, bootstrap) available. These tests therefore
run in the reproducers phase, and its functions are coroutines that must
be awaited. The pure-Python fallback (tests.dis.dis) is exercised
indirectly wherever other tests call it for debugging output.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import io
import unittest

import pytest

from koskari.importer import install
from koskari.lib import (
    BoundMethod, Code, CoreType, FormalsSpec, NamespaceSpec, OpCode)
from tests import make_callable


pytestmark = pytest.mark.phase_reproducers

_dis_mod = None


def _dis():
    """
    Return koskari.dis, importing on first use so collection does not
    import it before the import hook is installed
    """

    # just in case, some tests may have uninstalled the importer
    install()

    global _dis_mod
    if _dis_mod is None:
        import koskari.dis as dis
        _dis_mod = dis
    return _dis_mod


def _make_truthy_code():
    """
    Code: VAR YUS, jump-if-true to 4, else CONST NO, JUMP 5, CONST YES, RETURN
    """
    instr = (
        [OpCode.VAR, "YUS"],
        [OpCode.JUMP_TRUE, "truthy"],
        [OpCode.CONST, "NO"],
        [OpCode.JUMP, "done"],
        [OpCode.LABEL, "truthy"],
        [OpCode.CONST, "YES"],
        [OpCode.LABEL, "done"],
        [OpCode.RETURN, 1],
    )
    nspec = NamespaceSpec(None, (), ())
    return Code(nspec, instr, "truthy_yus", "something.ki")


def _make_push_ns_code():
    """
    Code that emits PUSH_NS with nspec and names=None in the EXTRA row,
    then POP_NS, so we can verify the namespace annotation on PUSH_NS.
    """
    child_nspec = NamespaceSpec("let", ("x", "y"), ("outer",))
    instr = (
        [OpCode.PUSH_NS, (child_nspec, None, 0, 0)],
        [OpCode.CONST, None],
        [OpCode.POP_NS, 1],
        [OpCode.RETURN, 1],
    )
    outer_nspec = NamespaceSpec("outer", ("outer",), ())
    return Code(outer_nspec, instr, "push_ns_fn", "file.ki")


def _make_fast_code():
    """
    Code that uses FAST (local slot 0) and FAST (closure slot 2, which is
    closure index 0 when there are 2 locals).
    """
    local_syms = ("a", "b")
    closure_syms = ("outer",)
    nspec = NamespaceSpec("fn", local_syms, closure_syms)
    instr = (
        [OpCode.FAST, 0],
        [OpCode.FAST, 2],
        [OpCode.RETURN, 1],
    )
    return Code(nspec, instr, "fast_fn", "file.ki")


def _make_operand_code():
    """
    Code with a packed OPCOUNT arg on OBJECT_OPERAND (BIT_AND over two
    operands) and an OPERAND field on a FAST_INPLACE row (ADD), so the
    packed value renders by operator name and count and the bare one by
    operator name.
    """
    nspec = NamespaceSpec("fn", ("a", "b"), ())
    instr = (
        [OpCode.FAST, 0],
        [OpCode.FAST, 1],
        [OpCode.OBJECT_OPERAND, (4 << 8) | 2],
        [OpCode.FAST_INPLACE, (0, 0, 0, 0)],
        [OpCode.FAST, 0],
        [OpCode.RETURN, 1],
    )
    return Code(nspec, instr, "operand_fn", "file.ki")


def _make_compare_code():
    """
    Code with the two compare-check row shapes, each with the negate
    flag set and clear: a bare CHECK_COMPARE row carries the flag in its
    fourth field, a CHECK_COMPARE_FAST row in bit 6 of its CMPSRC field
    (LT selector 0, both operands fast slots). A set flag renders the
    selector as the negated comparison. The OBJECT_COMPARE_VAR row has
    a CMP field too, with a pool index where the check keeps its flag.
    """

    nspec = NamespaceSpec("fn", ("a", "b"), ())
    instr = (
        [OpCode.FAST, 0],
        [OpCode.FAST, 1],
        [OpCode.CHECK_COMPARE, (0, 2, "held", 1)],
        [OpCode.FAST, 0],
        [OpCode.FAST, 1],
        [OpCode.CHECK_COMPARE, (0, 2, "held", 0)],
        [OpCode.CHECK_COMPARE_FAST, (0x40, 0, 1, "held")],
        [OpCode.CHECK_COMPARE_FAST, (0, 0, 1, "held")],
        [OpCode.LABEL, "held"],
        [OpCode.OBJECT_COMPARE_VAR, (0, 0, "a", 100)],
        [OpCode.RETURN, 1],
    )
    return Code(nspec, instr, "compare_fn", "file.ki")


def _make_core_instruction_code():
    """
    Code with a core-type test and a slot clear, so
    the ordinal renders by name, the indices as a tuple and the depth
    as a number
    """
    nspec = NamespaceSpec("fn", (), ())
    instr = (
        [OpCode.CONST, (1,)],
        [OpCode.MATCH_DESCEND, "miss"],
        [OpCode.MATCH_SCOPE, (0, 2, 0, 0)],
        [OpCode.MATCH_CLEAR, (0, (0, 1), 0, 0)],
        [OpCode.MATCH_NEXT, ("miss", 1, 0, 0)],
        [OpCode.MATCH_CORE, ("miss", CoreType.INT, 0, 0)],
        [OpCode.LABEL, "miss"],
        [OpCode.RETURN, 1],
    )
    return Code(nspec, instr, "core_instruction_fn", "file.ki")


def _make_expression():
    """
    Expression wrapping the truthy code, for _get_code / dis unwrapping.
    """
    code = _make_truthy_code()
    base_ns = NamespaceSpec(None, (), ()).get_namespace(None)
    return make_callable(code, base_ns)


def _make_function():
    """
    Function with (x, y) formals, base_ns with a closure binding.
    """
    code = _make_truthy_code()
    nspec = NamespaceSpec(None, ("outer",), ())
    base_ns = nspec.get_namespace(None)
    base_ns.store("outer", 99)
    return make_callable(code, base_ns, FormalsSpec(("x",), None, ("y",)))


class TestGetCode(unittest.IsolatedAsyncioTestCase):

    async def test_code_passthrough(self):
        code = _make_truthy_code()
        self.assertIs(await _dis()._get_code(code), code)

    async def test_expression_unwraps_to_code(self):
        expr = _make_expression()
        self.assertIs(await _dis()._get_code(expr), expr.code)

    async def test_non_code_returns_none(self):
        self.assertIs(await _dis()._get_code(42), None)
        self.assertIs(await _dis()._get_code(type("NoCode", (), {})()), None)


class TestDisOutput(unittest.IsolatedAsyncioTestCase):

    async def test_dis_truthy_yus(self):
        code = _make_truthy_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("KosCode", text)
        self.assertIn("truthy_yus", text)
        self.assertIn("Constant pool (3):", text)
        self.assertIn("YUS", text)
        self.assertIn("NO", text)
        self.assertIn("YES", text)
        self.assertIn("Vars (local_syms", text)
        self.assertIn("Closure refs", text)
        self.assertIn("Instructions (6):", text)
        self.assertIn("VAR", text)
        self.assertIn("0 (", text)
        self.assertIn("JUMP_TRUE", text)
        self.assertIn("(-> 4)", text)
        self.assertIn("RETURN", text)

    async def test_dis_packed_operand_args(self):
        code = _make_operand_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("OBJECT_OPERAND", text)
        self.assertIn("1026 (BIT_AND x2)", text)
        self.assertIn("FAST_INPLACE", text)
        self.assertIn('(ADD, fast 0 ("a"), 0, 0)', text)

    async def test_dis_marks_negate_compare_rows(self):
        code = _make_compare_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        lines = [l for l in buf.getvalue().splitlines() if "CHECK_COMPARE" in l]
        self.assertEqual(len(lines), 4)
        self.assertIn("(not LT, 2, -> 8, 1)", lines[0])
        self.assertIn("(LT, 2, -> 8, 0)", lines[1])
        self.assertIn('(not LT, fast 0 ("a"), fast 1 ("b"), -> 8)', lines[2])
        self.assertIn('(LT, fast 0 ("a"), fast 1 ("b"), -> 8)', lines[3])

    async def test_dis_value_compare_keeps_its_selector(self):
        code = _make_compare_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        lines = [l for l in buf.getvalue().splitlines()
                 if "OBJECT_COMPARE_VAR" in l]
        self.assertEqual(len(lines), 1)
        self.assertIn("(LT, ", lines[0])
        self.assertNotIn("not", lines[0])

    async def test_dis_core_instructions(self):
        code = _make_core_instruction_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("(-> 6, INT, 0, 0)", text)
        self.assertIn("(0, c#1 (#(0 1)), 0, 0)", text)
        self.assertIn("(-> 6, 1, 0, 0)", text)

    async def test_dis_expression(self):
        """
        Expression input disassembles its wrapped code without a Function
        or Bound-method leading section
        """
        expr = _make_expression()
        buf = io.StringIO()
        await _dis().dis(expr, file=buf)
        text = buf.getvalue()
        self.assertIn("KosCode", text)
        self.assertIn("truthy_yus", text)
        self.assertNotIn("Function instance:", text)
        self.assertNotIn("Bound method instance:", text)

    async def test_dis_with_locals_and_closures(self):
        local_syms = ("a", "b")
        closure_syms = ("outer",)
        nspec = NamespaceSpec(None, local_syms, closure_syms)
        instr = (
            [OpCode.FAST_REF, 0],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr, "fn", "file.ki")
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("Vars (local_syms, 2):", text)
        self.assertIn('"a"', text)
        self.assertIn('"b"', text)
        self.assertIn("Closure refs (1):", text)
        self.assertIn('"outer"', text)

    async def test_nspec_in_constant_pool_is_expanded(self):
        """
        NamespaceSpec entries in the constant pool are shown with ns# label
        and their locals/closures lines rather than a bare repr
        """
        code = _make_push_ns_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("ns#0:", text)
        self.assertIn('NamespaceSpec "let"', text)
        self.assertIn("locals:", text)
        self.assertIn("closures:", text)
        self.assertIn('#("x" "y")', text)
        self.assertIn('#("outer")', text)

    async def test_push_ns_annotated_with_ns_id(self):
        """
        PUSH_NS instructions show the ns# label and namespace name
        """
        code = _make_push_ns_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        self.assertIn("PUSH_NS", text)
        push_ns_line = next(
            line for line in text.splitlines()
            if "PUSH_NS" in line
        )
        self.assertIn("ns#0", push_ns_line)
        self.assertIn('"let"', push_ns_line)

    async def test_fast_local_label(self):
        """
        FAST accessing a local slot shows 'local' prefix
        """
        code = _make_fast_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        lines = [l for l in text.splitlines() if "FAST" in l]
        self.assertTrue(any('local "a"' in l for l in lines))

    async def test_fast_closure_label(self):
        """
        FAST accessing a closure slot shows 'closure' prefix
        """
        code = _make_fast_code()
        buf = io.StringIO()
        await _dis().dis(code, file=buf)
        text = buf.getvalue()
        lines = [l for l in text.splitlines() if "FAST" in l]
        self.assertTrue(any('closure "outer"' in l for l in lines))


class TestDisFunction(unittest.IsolatedAsyncioTestCase):

    async def test_dis_function_adds_leading_section(self):
        """
        Function input prints Function instance section (name, formals,
        base_ns) before KosCode disassembly
        """
        fn = _make_function()
        buf = io.StringIO()
        await _dis().dis(fn, file=buf)
        text = buf.getvalue()
        self.assertIn("Function instance:", text)
        self.assertIn("  name:", text)
        self.assertIn("  formals:", text)
        self.assertIn("  closures:", text)
        self.assertIn("KosCode", text)
        self.assertIn("truthy_yus", text)
        self.assertIn("VAR", text)

    async def test_dis_function_then_code(self):
        """
        Dis of Function includes leading block; dis of same Code does not
        """
        code = _make_truthy_code()
        nspec = NamespaceSpec(None, (), ())
        base_ns = nspec.get_namespace(None)
        fn = make_callable(code, base_ns, FormalsSpec())
        buf_fn = io.StringIO()
        await _dis().dis(fn, file=buf_fn)
        buf_code = io.StringIO()
        await _dis().dis(code, file=buf_code)
        fn_text = buf_fn.getvalue()
        code_text = buf_code.getvalue()
        self.assertIn("Function instance:", fn_text)
        self.assertNotIn("Function instance:", code_text)
        self.assertIn("KosCode", fn_text)
        self.assertIn("KosCode", code_text)
        self.assertIn("truthy_yus", fn_text)
        self.assertIn("truthy_yus", code_text)

    async def test_dis_non_code_raises(self):
        """
        dis() raises TypeError for non-Code, non-Function input
        """
        with self.assertRaises(TypeError):
            await _dis().dis(42)
        with self.assertRaises(TypeError):
            await _dis().dis("not code")


class TestDisBoundMethod(unittest.IsolatedAsyncioTestCase):

    async def test_dis_bound_method(self):
        """
        BoundMethod prints bound section, function section, then same KosCode
        as the underlying Function.
        """
        fn = _make_function()
        obj = object()
        bm = fn.__get__(obj, type(obj))
        self.assertIsInstance(bm, BoundMethod)
        buf = io.StringIO()
        await _dis().dis(bm, file=buf)
        text = buf.getvalue()
        self.assertIn("Bound method instance:", text)
        self.assertIn("__self__:", text)
        self.assertIn(await _dis()._format_constant(obj), text)
        self.assertIn("Function instance:", text)
        self.assertIn("KosCode", text)
        self.assertIn("truthy_yus", text)
        self.assertIn("VAR", text)

    async def test_dis_bound_method_not_plain_function(self):
        """
        Bound-method disassembly includes 'Bound method'; plain Function does
        not.
        """
        fn = _make_function()
        obj = object()
        bm = fn.__get__(obj, type(obj))
        buf_fn = io.StringIO()
        await _dis().dis(fn, file=buf_fn)
        buf_bm = io.StringIO()
        await _dis().dis(bm, file=buf_bm)
        self.assertNotIn("Bound method instance:", buf_fn.getvalue())
        self.assertIn("Bound method instance:", buf_bm.getvalue())


class TestGetInstructionLines(unittest.IsolatedAsyncioTestCase):

    async def test_instruction_lines_match_oparg(self):
        code = _make_truthy_code()
        lines = [line async for line in _dis()._get_instruction_lines(code)]
        self.assertEqual(len(lines), 6)
        self.assertEqual(lines[0][0], 0)
        self.assertEqual(lines[0][1], "VAR")
        self.assertIn("0 (", lines[0][2])
        self.assertIn("YUS", lines[0][2])
        self.assertEqual(lines[1][1], "JUMP_TRUE")
        self.assertIn("(-> 4)", lines[1][2])

    async def test_push_ns_in_instruction_lines(self):
        """
        _get_instruction_lines includes ns# annotation for PUSH_NS
        """
        code = _make_push_ns_code()
        lines = [line async for line in _dis()._get_instruction_lines(code)]
        push_ns = next(
            (idx, name, arg) for idx, name, arg in lines
            if name == "PUSH_NS"
        )
        self.assertIn("ns#0", push_ns[2])
        self.assertIn('"let"', push_ns[2])

    async def test_fast_local_in_instruction_lines(self):
        """
        _get_instruction_lines shows 'local' for fast local slots
        """
        code = _make_fast_code()
        lines = [line async for line in _dis()._get_instruction_lines(code)]
        fast_lines = [(n, a) for _, n, a in lines if n == "FAST"]
        self.assertTrue(any('local "a"' in a for _, a in fast_lines))

    async def test_fast_closure_in_instruction_lines(self):
        """
        _get_instruction_lines shows 'closure' for fast closure slots
        """
        code = _make_fast_code()
        lines = [line async for line in _dis()._get_instruction_lines(code)]
        fast_lines = [(n, a) for _, n, a in lines if n == "FAST"]
        self.assertTrue(any('closure "outer"' in a for _, a in fast_lines))


class TestFormatConstant(unittest.IsolatedAsyncioTestCase):

    async def test_short_repr_unchanged(self):
        self.assertEqual(await _dis()._format_constant(42), "42")
        self.assertEqual(await _dis()._format_constant("x"), '"x"')

    async def test_long_repr_truncated(self):
        long_str = "x" * 70
        out = await _dis()._format_constant(long_str)
        self.assertEqual(len(out), 60)
        self.assertTrue(out.endswith("..."))


class TestBuildNsConstMap(unittest.IsolatedAsyncioTestCase):

    async def test_push_ns_maps_extra_row_nspec(self):
        code = _make_push_ns_code()
        ns_map = await _dis()._build_ns_const_map(code)
        self.assertIn(0, ns_map)
        self.assertEqual(ns_map[0], 0)

    async def test_no_push_ns_gives_empty_map(self):
        code = _make_truthy_code()
        ns_map = await _dis()._build_ns_const_map(code)
        self.assertEqual(ns_map, {})

    async def test_mapped_entries_point_at_nspec(self):
        """
        Every entry in the ns_const_map resolves to a NamespaceSpec in the
        constant pool
        """
        code = _make_push_ns_code()
        ns_map = await _dis()._build_ns_const_map(code)
        cpool = code.constants_pool
        for _i, cpool_idx in ns_map.items():
            self.assertIsInstance(cpool[cpool_idx], NamespaceSpec)


class TestFormatNspecInline(unittest.IsolatedAsyncioTestCase):

    async def test_named_nspec(self):
        nspec = NamespaceSpec("let", ("a",), ())

        # uses the kos_repr, so strings are double-quoted
        self.assertEqual(
            await _dis()._format_nspec_inline(5, nspec),
            'ns#5 "let"')

    async def test_none_named_nspec(self):
        nspec = NamespaceSpec(None, (), ())
        self.assertEqual(
            await _dis()._format_nspec_inline(0, nspec),
            "ns#0 None")


# The end.
