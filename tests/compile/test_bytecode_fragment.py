# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
Unit tests for Code bytecode fragment API.

Python API for creating bytecode objects from instruction fragments,
ported from sibilant-ceval. Enables constructing Code objects from
sequences of [opcode, arg] pairs with label resolution, constant pool
deduplication, and position tracking.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.compile import ByteCodeFragment
from koskari.lib import Code, FormalsSpec, FunctionSpec, NamespaceSpec, OpCode, symbol


pytestmark = pytest.mark.phase_compile


def example_bytecode_truthy_yus():
    """Example Code: VAR YUS, jump-if-truthy to truthy, else NO, jump done."""
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


class TestCode(unittest.IsolatedAsyncioTestCase):
    """Tests for the Code type and fragment-based construction."""

    async def test_fields(self):
        """Code stores name, filename, namespace_spec, yields, and uses_fast."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, None], [OpCode.RETURN, 1])
        code = Code(nspec, instr, "testing")
        self.assertEqual(code.name, "testing")
        self.assertEqual(code.filename, "<anon>")
        self.assertIs(code.namespace_spec, nspec)
        self.assertEqual(len(code.constants_pool), 1)
        self.assertFalse(code.yields)
        self.assertFalse(code.uses_fast)

    async def test_uses_fast_flag(self):
        """Code.uses_fast is True when FAST opcodes appear at root scope."""
        nspec_fast = NamespaceSpec(None, ("x",), ())
        instr_fast = (
            [OpCode.FAST, 0],
            [OpCode.RETURN, 1],
        )
        code_fast = Code(nspec_fast, instr_fast)
        self.assertTrue(code_fast.uses_fast)

        nspec_nofast = NamespaceSpec(None, ("x",), ())
        instr_nofast = (
            [OpCode.CONST, 42],
            [OpCode.RETURN, 1],
        )
        code_nofast = Code(nspec_nofast, instr_nofast)
        self.assertFalse(code_nofast.uses_fast)

    async def test_transform(self):
        """Instruction fragments are resolved: labels, constants, opargs."""
        bc = example_bytecode_truthy_yus()
        self.assertEqual(bc.name, "truthy_yus")
        self.assertEqual(bc.oparg(0), (OpCode.VAR, 0))
        self.assertEqual(bc.oparg(1), (OpCode.JUMP_TRUE, 4))
        self.assertEqual(bc.oparg(2), (OpCode.CONST, 1))
        self.assertEqual(bc.oparg(3), (OpCode.JUMP, 5))
        self.assertEqual(bc.oparg(4), (OpCode.CONST, 2))
        self.assertEqual(bc.oparg(5), (OpCode.RETURN, 1))
        self.assertRaises(IndexError, bc.oparg, 8)
        self.assertEqual(bc.constant(0), "YUS")
        self.assertEqual(bc.constant(1), "NO")
        self.assertEqual(bc.constant(2), "YES")
        self.assertRaises(IndexError, bc.constant, 3)

    async def test_distinct_consts(self):
        """Constant pool deduplicates; VAR and CONST share by identity."""
        instr = (
            [OpCode.VAR, "NO"],
            [OpCode.CONST, "NO"],
            [OpCode.CONST, "YES"],
            [OpCode.CONST, -1],
            [OpCode.CONST, 1],
            [OpCode.CONST, True],
            [OpCode.CONST, None],
            [OpCode.CONST, False],
            [OpCode.CONST, 0],
            [OpCode.CONST, 0],
            [OpCode.RETURN, 1],
        )
        nspec = NamespaceSpec(None, (), ())
        bc = Code(nspec, instr)
        self.assertEqual(bc.length(), 11)
        self.assertEqual(bc.oparg(0), (OpCode.VAR, 0))
        self.assertEqual(bc.oparg(1), (OpCode.CONST, 0))
        self.assertEqual(bc.oparg(2), (OpCode.CONST, 1))
        self.assertEqual(bc.oparg(3), (OpCode.CONST, 2))
        self.assertEqual(bc.oparg(4), (OpCode.CONST, 3))
        self.assertEqual(bc.oparg(5), (OpCode.CONST, 4))
        self.assertEqual(bc.oparg(6), (OpCode.CONST, 5))
        self.assertEqual(bc.oparg(7), (OpCode.CONST, 6))
        self.assertEqual(bc.oparg(8), (OpCode.CONST, 7))
        self.assertEqual(bc.oparg(9), (OpCode.CONST, 7))
        self.assertEqual(bc.oparg(10), (OpCode.RETURN, 1))
        self.assertRaises(IndexError, bc.oparg, 11)
        self.assertIs(bc.constant(0), "NO")
        self.assertIs(bc.constant(1), "YES")
        self.assertIs(bc.constant(2), -1)
        self.assertIs(bc.constant(3), 1)
        self.assertIs(bc.constant(4), True)
        self.assertIs(bc.constant(5), None)
        self.assertIs(bc.constant(6), False)
        self.assertIs(bc.constant(7), 0)
        self.assertRaises(IndexError, bc.constant, 8)
        self.assertEqual(bc.constants_pool,
                         ("NO", "YES", -1, 1, True, None, False, 0))

    async def test_return_constant(self):
        """Simple Code: CONST 42, RETURN 1."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, 42], [OpCode.RETURN, 1])
        code = Code(nspec, instr, "return_42")
        self.assertEqual(code.length(), 2)
        self.assertEqual(code.oparg(0), (OpCode.CONST, 0))
        self.assertEqual(code.oparg(1), (OpCode.RETURN, 1))
        self.assertEqual(code.constant(0), 42)

    async def test_return_none(self):
        """Simple Code: CONST None, RETURN 1."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, None], [OpCode.RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.constant(0), None)

    async def test_return_string(self):
        """Simple Code: CONST string, RETURN 1."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, "hello"], [OpCode.RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.constant(0), "hello")

    async def test_position(self):
        """Code with POSITION instruction updates source position."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.POSITION, (1, 1)],
            [OpCode.CONST, 1],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        line, col = code.position(0)
        self.assertEqual((line, col), (1, 1))

    async def test_var_instruction(self):
        """Code with VAR instruction stores name in constant pool."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.VAR, "x"],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.oparg(0), (OpCode.VAR, 0))
        self.assertEqual(code.constant(0), "x")

    async def test_backward_jump(self):
        """Jump backward to earlier LABEL. LABEL opcodes become NOOP."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.LABEL, "loop"],
            [OpCode.CONST, 0],
            [OpCode.JUMP_FALSE, "done"],
            [OpCode.JUMP, "loop"],
            [OpCode.LABEL, "done"],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 4)
        self.assertEqual(code.oparg(0), (OpCode.CONST, 0))
        self.assertEqual(code.oparg(1), (OpCode.JUMP_FALSE, 3))
        self.assertEqual(code.oparg(2), (OpCode.JUMP, 0))
        self.assertEqual(code.oparg(3), (OpCode.RETURN, 1))

    async def test_empty_instructions_rejected(self):
        """Code with empty instructions raises ValueError."""
        nspec = NamespaceSpec(None, (), ())
        with self.assertRaises(ValueError):
            Code(nspec, [])

    async def test_undefined_label_raises(self):
        """Jump to undefined label raises RuntimeError."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.JUMP, "missing"],
            [OpCode.RETURN, 1],
        )
        with self.assertRaises(RuntimeError):
            Code(nspec, instr)

    async def test_length_and_position_bounds(self):
        """length() and position() respect instruction count."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, 1], [OpCode.RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        with self.assertRaises(IndexError):
            code.oparg(2)
        with self.assertRaises(IndexError):
            code.constant(1)

    async def test_filename_default(self):
        """Code with no filename uses default."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, 1], [OpCode.RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.filename, "<anon>")

    async def test_filename_explicit(self):
        """Code accepts explicit filename."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, 1], [OpCode.RETURN, 1])
        code = Code(nspec, instr, "fn", "script.ki")
        self.assertEqual(code.filename, "script.ki")
        self.assertEqual(code.name, "fn")

    async def test_extra_opcode_call_fast_attr_names(self):
        """EXTRA opcode CALL_FAST_ATTR_NAMES: arg indexes extra table row."""
        nspec = NamespaceSpec(None, ("obj",), ())
        kw_names = ()
        instr = (
            [OpCode.CONST, "obj"],
            [OpCode.VAR, "obj"],
            [OpCode.CALL_FAST_ATTR_NAMES, (0, "bar", kw_names, 0)],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 4)
        op, arg = code.oparg(2)
        self.assertEqual(op, OpCode.CALL_FAST_ATTR_NAMES)
        self.assertEqual(arg, 0)
        a0, a1, a2, a3 = code.extra_tuple(0)
        self.assertEqual(a0, 0)
        self.assertIn(a1, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a1), "bar")
        self.assertIn(a2, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a2), kw_names)
        self.assertEqual(a3, 0)

    async def test_extra_opcode_call_var_attr_names(self):
        """EXTRA opcode CALL_VAR_ATTR_NAMES: arg indexes extra table row."""
        nspec = NamespaceSpec(None, ("obj",), ())
        kw_names = ()
        instr = (
            [OpCode.CALL_VAR_ATTR_NAMES, ("obj", "bar", kw_names, 0)],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        op, arg = code.oparg(0)
        self.assertEqual(op, OpCode.CALL_VAR_ATTR_NAMES)
        self.assertEqual(arg, 0)
        a0, a1, a2, a3 = code.extra_tuple(0)
        self.assertIn(a0, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a0), "obj")
        self.assertIn(a1, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a1), "bar")
        self.assertIn(a2, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a2), kw_names)
        self.assertEqual(a3, 0)

    async def test_extra_opcode_call_attr(self):
        """EXTRA opcode CALL_ATTR: arg indexes extra table row (attr, count)."""
        nspec = NamespaceSpec(None, (), ())
        attr_name = "upper"
        instr = (
            [OpCode.CONST, "hello"],
            [OpCode.CALL_ATTR, (attr_name, 0, 0, 0)],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 3)
        op, arg = code.oparg(1)
        self.assertEqual(op, OpCode.CALL_ATTR)
        self.assertEqual(arg, 0)
        a0, a1, a2, a3 = code.extra_tuple(0)
        self.assertIn(a0, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a0), attr_name)
        self.assertEqual(a1, 0)
        self.assertEqual(a2, 0)
        self.assertEqual(a3, 0)

    async def test_extra_opcode_var_attr(self):
        """EXTRA opcode VAR_ATTR: arg indexes extra table row (var, attr)."""
        nspec = NamespaceSpec(None, ("obj",), ())
        var_name = "obj"
        attr_name = "value"
        instr = (
            [OpCode.VAR_ATTR, (var_name, attr_name, 0, 0)],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        op, arg = code.oparg(0)
        self.assertEqual(op, OpCode.VAR_ATTR)
        self.assertEqual(arg, 0)
        a0, a1, a2, a3 = code.extra_tuple(0)
        self.assertIn(a0, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a0), var_name)
        self.assertIn(a1, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a1), attr_name)
        self.assertEqual(a2, 0)
        self.assertEqual(a3, 0)

    async def test_extra_opcode_fast_attr(self):
        """EXTRA opcode FAST_ATTR: arg indexes extra table row (slot, attr)."""
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = (
            [OpCode.FAST_ATTR, (0, "value", 0, 0)],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        op, arg = code.oparg(0)
        self.assertEqual(op, OpCode.FAST_ATTR)
        self.assertEqual(arg, 0)
        a0, a1, a2, a3 = code.extra_tuple(0)
        self.assertEqual(a0, 0)
        self.assertIn(a1, range(len(code.constants_pool)))
        self.assertEqual(code.constant(a1), "value")
        self.assertEqual(a2, 0)
        self.assertEqual(a3, 0)

    async def test_precise_sizing_duplicate_constants(self):
        """cpool_len equals unique identity count; duplicates share slot."""
        nspec = NamespaceSpec(None, (), ())
        shared = 42
        instr = (
            [OpCode.CONST, shared],
            [OpCode.CONST, shared],
            [OpCode.CONST, shared],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(len(code.constants_pool), 1)
        self.assertEqual(code.oparg(0), (OpCode.CONST, 0))
        self.assertEqual(code.oparg(1), (OpCode.CONST, 0))
        self.assertEqual(code.oparg(2), (OpCode.CONST, 0))

    async def test_allocation_introspection_fields_basic(self):
        """Allocation fields exist and satisfy the identity relation."""

        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.CONST, 1], [OpCode.RETURN, 1])
        code = Code(nspec, instr, "alloc_basic")
        fields = (
            'allocation_bytes',
            'payload_bytes',
            'padding_bytes',
            'page_rounding_bytes',
            'code_bytes',
            'position_bytes',
            'cpool_bytes',
            'extra_bytes',
            'extent_bytes',
            'top_extent_bytes',
        )

        for field in fields:
            value = getattr(code, field)
            self.assertIsInstance(value, int)
            self.assertGreaterEqual(value, 0)

        self.assertEqual(
            code.allocation_bytes,
            code.payload_bytes + code.padding_bytes + code.page_rounding_bytes)

    async def test_allocation_introspection_with_extent_and_extra(self):
        """Fields remain coherent when code has extents and EXTRA rows."""

        nspec = NamespaceSpec(None, ('obj',), ())
        instr = (
            [OpCode.PUSH_LOOP, 0],
            [OpCode.FAST_ATTR, (0, 'value', 0, 0)],
            [OpCode.POP_LOOP, 0],
            [OpCode.RETURN, 1],
        )
        code = Code(nspec, instr, "alloc_complex")

        self.assertGreaterEqual(code.extra_bytes, 1)
        self.assertGreaterEqual(code.extent_bytes, 1)
        self.assertEqual(
            code.allocation_bytes,
            code.payload_bytes + code.padding_bytes + code.page_rounding_bytes)


class TestCodeFunctionSpec(unittest.IsolatedAsyncioTestCase):
    """
    Tests for Code.function_spec(formals_spec) and FunctionSpec type
    """

    async def test_function_spec_type_exists(self):
        """FunctionSpec is registered from koskari.lib"""

        self.assertIsNotNone(FunctionSpec)
        self.assertEqual(FunctionSpec.__name__, "FunctionSpec")
        self.assertEqual(FunctionSpec.__module__, "koskari.lib")

    async def test_code_function_spec_returns_function_spec(self):
        """Code.function_spec(FormalsSpec()) returns a FunctionSpec"""

        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.RETURN, 0],)
        code = Code(nspec, instr, "inner")
        fspec = FormalsSpec()
        result = code.function_spec(fspec)
        self.assertIsInstance(result, FunctionSpec)
        self.assertIs(result.code, code)
        self.assertIs(result.formals_spec, fspec)

    async def test_code_function_spec_requires_formals_spec(self):
        """Code.function_spec(non-FormalsSpec) raises TypeError"""

        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.RETURN, 0],)
        code = Code(nspec, instr, "inner")
        with self.assertRaises(TypeError):
            code.function_spec(None)
        with self.assertRaises(TypeError):
            code.function_spec("not a formals spec")

    async def test_code_extras_is_mutable_dict(self):
        """Code.extras is a dict; compiler may set doc metadata"""

        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.RETURN, 0],)
        code = Code(nspec, instr, "inner")
        self.assertIsInstance(code.extras, dict)
        code.extras['doc'] = 'stored'
        self.assertEqual(code.extras['doc'], 'stored')


class TestByteCodeFragment(unittest.IsolatedAsyncioTestCase):
    """Tests for ByteCodeFragment: instructions, requests, declares."""

    async def test_init_defaults(self):
        f = ByteCodeFragment()
        self.assertIsNone(f.name)
        self.assertIsNone(f.filename)
        self.assertIsNone(f.compiler)
        self.assertEqual(f.instructions, [])
        self.assertEqual(f.requests, [])
        self.assertEqual(f.declares, [])

    async def test_init_args(self):
        f = ByteCodeFragment(name="block", filename="x.ki")
        self.assertEqual(f.name, "block")
        self.assertEqual(f.filename, "x.ki")
        self.assertIsNone(f.compiler)

    async def test_declare_var(self):
        f = ByteCodeFragment()
        f.declare_var("a")
        self.assertEqual(f.declares, ["a"])
        f.declare_var("b")
        self.assertEqual(f.declares, ["a", "b"])
        f.declare_var("a")
        self.assertEqual(f.declares, ["a", "b"])

    async def test_declare_vars(self):
        f = ByteCodeFragment()
        f.declare_vars(("x", "y", "z"))
        self.assertEqual(f.declares, ["x", "y", "z"])
        f.declare_vars(["z", "w"])
        self.assertEqual(f.declares, ["x", "y", "z", "w"])

    async def test_request_var(self):
        f = ByteCodeFragment()
        f.request_var("outer")
        self.assertEqual(f.requests, ["outer"])
        f.request_var("other")
        self.assertEqual(f.requests, ["outer", "other"])
        f.request_var("outer")
        self.assertEqual(f.requests, ["outer", "other"])

    async def test_request_var_skipped_if_declared(self):
        f = ByteCodeFragment()
        f.declare_var("local")
        f.request_var("local")
        self.assertEqual(f.declares, ["local"])
        self.assertEqual(f.requests, [])

    async def test_instruction(self):
        f = ByteCodeFragment()
        f.instruction(OpCode.CONST, 42)
        f.instruction(OpCode.RETURN, 1)
        self.assertEqual(
            f.instructions,
            [(OpCode.CONST, 42), (OpCode.RETURN, 1)],
        )

    async def test_position(self):
        f = ByteCodeFragment()
        f.position((3, 10), extra="line text")
        self.assertEqual(
            f.instructions,
            [(OpCode.POSITION, (3, 10, "line text"))],
        )

    async def test_position_of_no_position(self):
        f = ByteCodeFragment()
        f.position_of(object())
        self.assertEqual(f.instructions, [])

    async def test_as_namespace_spec(self):
        f = ByteCodeFragment(name="ns1")
        f.declare_var("a")
        f.declare_var("b")
        f.request_var("up")
        nspec = f.as_namespace_spec()
        self.assertIsInstance(nspec, NamespaceSpec)
        self.assertEqual(nspec.name, "ns1")
        self.assertEqual(nspec.local_syms, ("a", "b"))
        self.assertEqual(nspec.closure_syms, ("up",))

    async def test_merge(self):
        a = ByteCodeFragment(name="a")
        a.declare_var("x")
        a.request_var("y")
        a.instruction(OpCode.CONST, 1)
        b = ByteCodeFragment(name="b")
        b.declare_var("z")
        b.request_var("y")
        b.instruction(OpCode.RETURN, 1)
        a.merge(b)
        self.assertEqual(a.declares, ["x", "z"])
        self.assertEqual(a.requests, ["y"])
        self.assertEqual(
            a.instructions,
            [(OpCode.CONST, 1), (OpCode.RETURN, 1)],
        )

    async def test_declare_var_with_type_expr(self):
        f = ByteCodeFragment()
        t = symbol("int")
        f.declare_var("x", type_expr=t)
        self.assertEqual(f.declares, ["x"])
        self.assertEqual(f.annotation_bindings, [("x", t)])

    async def test_declare_var_without_type_expr_no_annotation_bindings(self):
        f = ByteCodeFragment()
        f.declare_var("a")
        self.assertEqual(f.annotation_bindings, [])

    async def test_merge_annotation_bindings(self):
        a = ByteCodeFragment(name="a")
        ta = symbol("int")
        a.declare_var("x", type_expr=ta)
        b = ByteCodeFragment(name="b")
        tb = symbol("str")
        b.declare_var("y", type_expr=tb)
        a.merge(b)
        self.assertEqual(a.annotation_bindings, [("x", ta), ("y", tb)])


# The end.
