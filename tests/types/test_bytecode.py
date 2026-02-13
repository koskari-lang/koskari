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

from koskari import (
    ByteCodeFragment,
    Code,
    FormalsSpec,
    NamespaceSpec,
    OpCode,
    unique_append,
)
from koskari.lib import FunctionSpec


def example_bytecode_truthy_yus():
    """Example Code: VAR YUS, jump-if-truthy to truthy, else NO, jump done."""
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


class TestCode(unittest.TestCase):
    """Tests for the Code type and fragment-based construction."""

    def test_fields(self):
        """Code stores name, filename, namespace_spec, and yields flag."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, None], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr, "testing")
        self.assertEqual(code.name, "testing")
        self.assertEqual(code.filename, "<anon>")
        self.assertIs(code.namespace_spec, nspec)
        self.assertEqual(len(code.constants_pool), 1)
        self.assertFalse(code.yields)

    def test_transform(self):
        """Instruction fragments are resolved: labels, constants, opargs."""
        bc = example_bytecode_truthy_yus()
        self.assertEqual(bc.name, "truthy_yus")
        self.assertEqual(bc.oparg(0), (OpCode.KOS_OP_VAR, 0))
        self.assertEqual(bc.oparg(1), (OpCode.KOS_OP_JUMP_TRUE, 4))
        self.assertEqual(bc.oparg(2), (OpCode.KOS_OP_CONST, 1))
        self.assertEqual(bc.oparg(3), (OpCode.KOS_OP_JUMP, 5))
        self.assertEqual(bc.oparg(4), (OpCode.KOS_OP_CONST, 2))
        self.assertEqual(bc.oparg(5), (OpCode.KOS_OP_RETURN, 1))
        self.assertRaises(IndexError, bc.oparg, 8)
        self.assertEqual(bc.constant(0), "YUS")
        self.assertEqual(bc.constant(1), "NO")
        self.assertEqual(bc.constant(2), "YES")
        self.assertRaises(IndexError, bc.constant, 3)

    def test_distinct_consts(self):
        """Constant pool deduplicates; VAR and CONST share by identity."""
        instr = (
            [OpCode.KOS_OP_VAR, "NO"],
            [OpCode.KOS_OP_CONST, "NO"],
            [OpCode.KOS_OP_CONST, "YES"],
            [OpCode.KOS_OP_CONST, -1],
            [OpCode.KOS_OP_CONST, 1],
            [OpCode.KOS_OP_CONST, True],
            [OpCode.KOS_OP_CONST, None],
            [OpCode.KOS_OP_CONST, False],
            [OpCode.KOS_OP_CONST, 0],
            [OpCode.KOS_OP_CONST, 0],
            [OpCode.KOS_OP_RETURN, 1],
        )
        nspec = NamespaceSpec(None, (), ())
        bc = Code(nspec, instr)
        self.assertEqual(bc.length(), 11)
        self.assertEqual(bc.oparg(0), (OpCode.KOS_OP_VAR, 0))
        self.assertEqual(bc.oparg(1), (OpCode.KOS_OP_CONST, 0))
        self.assertEqual(bc.oparg(2), (OpCode.KOS_OP_CONST, 1))
        self.assertEqual(bc.oparg(3), (OpCode.KOS_OP_CONST, 2))
        self.assertEqual(bc.oparg(4), (OpCode.KOS_OP_CONST, 3))
        self.assertEqual(bc.oparg(5), (OpCode.KOS_OP_CONST, 4))
        self.assertEqual(bc.oparg(6), (OpCode.KOS_OP_CONST, 5))
        self.assertEqual(bc.oparg(7), (OpCode.KOS_OP_CONST, 6))
        self.assertEqual(bc.oparg(8), (OpCode.KOS_OP_CONST, 7))
        self.assertEqual(bc.oparg(9), (OpCode.KOS_OP_CONST, 7))
        self.assertEqual(bc.oparg(10), (OpCode.KOS_OP_RETURN, 1))
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

    def test_return_constant(self):
        """Simple Code: CONST 42, RETURN 1."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, 42], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr, "return_42")
        self.assertEqual(code.length(), 2)
        self.assertEqual(code.oparg(0), (OpCode.KOS_OP_CONST, 0))
        self.assertEqual(code.oparg(1), (OpCode.KOS_OP_RETURN, 1))
        self.assertEqual(code.constant(0), 42)

    def test_return_none(self):
        """Simple Code: CONST None, RETURN 1."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, None], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.constant(0), None)

    def test_return_string(self):
        """Simple Code: CONST string, RETURN 1."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, "hello"], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.constant(0), "hello")

    def test_position(self):
        """Code with POSITION instruction updates source position."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.KOS_OP_POSITION, (1, 1)],
            [OpCode.KOS_OP_CONST, 1],
            [OpCode.KOS_OP_RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        line, col = code.position(0)
        self.assertEqual((line, col), (1, 1))

    def test_var_instruction(self):
        """Code with VAR instruction stores name in constant pool."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.KOS_OP_VAR, "x"],
            [OpCode.KOS_OP_RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.oparg(0), (OpCode.KOS_OP_VAR, 0))
        self.assertEqual(code.constant(0), "x")

    def test_backward_jump(self):
        """Jump backward to earlier LABEL. LABEL opcodes become NOOP."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.KOS_OP_LABEL, "loop"],
            [OpCode.KOS_OP_CONST, 0],
            [OpCode.KOS_OP_JUMP_FALSE, "done"],
            [OpCode.KOS_OP_JUMP, "loop"],
            [OpCode.KOS_OP_LABEL, "done"],
            [OpCode.KOS_OP_RETURN, 1],
        )
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 4)
        self.assertEqual(code.oparg(0), (OpCode.KOS_OP_CONST, 0))
        self.assertEqual(code.oparg(1), (OpCode.KOS_OP_JUMP_FALSE, 3))
        self.assertEqual(code.oparg(2), (OpCode.KOS_OP_JUMP, 0))
        self.assertEqual(code.oparg(3), (OpCode.KOS_OP_RETURN, 1))

    def test_empty_instructions_rejected(self):
        """Code with empty instructions raises ValueError."""
        nspec = NamespaceSpec(None, (), ())
        with self.assertRaises(ValueError):
            Code(nspec, [])

    def test_undefined_label_raises(self):
        """Jump to undefined label raises RuntimeError."""
        nspec = NamespaceSpec(None, (), ())
        instr = (
            [OpCode.KOS_OP_JUMP, "missing"],
            [OpCode.KOS_OP_RETURN, 1],
        )
        with self.assertRaises(RuntimeError):
            Code(nspec, instr)

    def test_length_and_position_bounds(self):
        """length() and position() respect instruction count."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, 1], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.length(), 2)
        with self.assertRaises(IndexError):
            code.oparg(2)
        with self.assertRaises(IndexError):
            code.constant(1)

    def test_filename_default(self):
        """Code with no filename uses default."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, 1], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr)
        self.assertEqual(code.filename, "<anon>")

    def test_filename_explicit(self):
        """Code accepts explicit filename."""
        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_CONST, 1], [OpCode.KOS_OP_RETURN, 1])
        code = Code(nspec, instr, "fn", "script.ki")
        self.assertEqual(code.filename, "script.ki")
        self.assertEqual(code.name, "fn")


class TestCodeFunctionSpec(unittest.TestCase):
    """
    Tests for Code.function_spec(formals_spec) and FunctionSpec type
    """

    def test_function_spec_type_exists(self):
        """FunctionSpec is registered from koskari.lib"""

        self.assertIsNotNone(FunctionSpec)
        self.assertEqual(FunctionSpec.__name__, "FunctionSpec")
        self.assertEqual(FunctionSpec.__module__, "koskari.lib")

    def test_code_function_spec_returns_function_spec(self):
        """Code.function_spec(FormalsSpec()) returns a FunctionSpec"""

        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_RETURN, 0],)
        code = Code(nspec, instr, "inner")
        fspec = FormalsSpec()
        result = code.function_spec(fspec)
        self.assertIsInstance(result, FunctionSpec)
        self.assertIs(result.code, code)
        self.assertIs(result.formals_spec, fspec)

    def test_code_function_spec_requires_formals_spec(self):
        """Code.function_spec(non-FormalsSpec) raises TypeError"""

        nspec = NamespaceSpec(None, (), ())
        instr = ([OpCode.KOS_OP_RETURN, 0],)
        code = Code(nspec, instr, "inner")
        with self.assertRaises(TypeError):
            code.function_spec(None)
        with self.assertRaises(TypeError):
            code.function_spec("not a formals spec")


class TestUniqueAppend(unittest.TestCase):
    """Tests for unique_append helper."""

    def test_append_when_absent(self):
        lst = []
        unique_append(lst, "a")
        self.assertEqual(lst, ["a"])
        unique_append(lst, "b")
        self.assertEqual(lst, ["a", "b"])

    def test_no_append_when_present(self):
        lst = ["a"]
        unique_append(lst, "a")
        self.assertEqual(lst, ["a"])

    def test_preserves_order(self):
        lst = []
        for x in ("b", "a", "b", "c", "a"):
            unique_append(lst, x)
        self.assertEqual(lst, ["b", "a", "c"])


class TestByteCodeFragment(unittest.TestCase):
    """Tests for ByteCodeFragment: instructions, requests, declares."""

    def test_init_defaults(self):
        f = ByteCodeFragment()
        self.assertIsNone(f.name)
        self.assertIsNone(f.filename)
        self.assertIsNone(f.mode)
        self.assertIsNone(f.compiler)
        self.assertEqual(f.instructions, [])
        self.assertEqual(f.requests, [])
        self.assertEqual(f.declares, [])

    def test_init_args(self):
        f = ByteCodeFragment(name="block", filename="x.ki", mode=1)
        self.assertEqual(f.name, "block")
        self.assertEqual(f.filename, "x.ki")
        self.assertEqual(f.mode, 1)
        self.assertIsNone(f.compiler)

    def test_declare_var(self):
        f = ByteCodeFragment()
        f.declare_var("a")
        self.assertEqual(f.declares, ["a"])
        f.declare_var("b")
        self.assertEqual(f.declares, ["a", "b"])
        f.declare_var("a")
        self.assertEqual(f.declares, ["a", "b"])

    def test_declare_vars(self):
        f = ByteCodeFragment()
        f.declare_vars(("x", "y", "z"))
        self.assertEqual(f.declares, ["x", "y", "z"])
        f.declare_vars(["z", "w"])
        self.assertEqual(f.declares, ["x", "y", "z", "w"])

    def test_request_var(self):
        f = ByteCodeFragment()
        f.request_var("outer")
        self.assertEqual(f.requests, ["outer"])
        f.request_var("other")
        self.assertEqual(f.requests, ["outer", "other"])
        f.request_var("outer")
        self.assertEqual(f.requests, ["outer", "other"])

    def test_request_var_skipped_if_declared(self):
        f = ByteCodeFragment()
        f.declare_var("local")
        f.request_var("local")
        self.assertEqual(f.declares, ["local"])
        self.assertEqual(f.requests, [])

    def test_instruction(self):
        f = ByteCodeFragment()
        f.instruction(OpCode.KOS_OP_CONST, 42)
        f.instruction(OpCode.KOS_OP_RETURN, 1)
        self.assertEqual(
            f.instructions,
            [(OpCode.KOS_OP_CONST, 42), (OpCode.KOS_OP_RETURN, 1)],
        )

    def test_position(self):
        f = ByteCodeFragment()
        f.position((3, 10), extra="line text")
        self.assertEqual(
            f.instructions,
            [(OpCode.KOS_OP_POSITION, (3, 10, "line text"))],
        )

    def test_position_of_no_position(self):
        f = ByteCodeFragment()
        f.position_of(object())
        self.assertEqual(f.instructions, [])

    def test_as_namespace_spec(self):
        f = ByteCodeFragment(name="ns1")
        f.declare_var("a")
        f.declare_var("b")
        f.request_var("up")
        nspec = f.as_namespace_spec()
        self.assertIsInstance(nspec, NamespaceSpec)
        self.assertEqual(nspec.name, "ns1")
        self.assertEqual(tuple(nspec.local_syms or ()), ("a", "b"))
        self.assertEqual(tuple(nspec.closure_syms or ()), ("up",))

    def test_merge(self):
        a = ByteCodeFragment(name="a")
        a.declare_var("x")
        a.request_var("y")
        a.instruction(OpCode.KOS_OP_CONST, 1)
        b = ByteCodeFragment(name="b")
        b.declare_var("z")
        b.request_var("y")
        b.instruction(OpCode.KOS_OP_RETURN, 1)
        a.merge(b)
        self.assertEqual(a.declares, ["x", "z"])
        self.assertEqual(a.requests, ["y"])
        self.assertEqual(
            a.instructions,
            [(OpCode.KOS_OP_CONST, 1), (OpCode.KOS_OP_RETURN, 1)],
        )


# The end.
