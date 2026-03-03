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
Tests for koskari.compile compile/import and compile/import-from special forms.

These tests inspect the generated opcode sequences without executing them at
the koskari VM level.  Execution of IMPORT / IMPORT_REL opcodes requires a
fully-built C extension, which is exercised by the importer integration tests
in tests/importer/.
"""


import unittest

from koskari.lib import OpCode
from koskari.exceptions import SpecialError

from . import SpecialsTestCase


class _OpcodeHelper:
    """
    Mixin: extract all instructions from a Code object as (OpCode, resolved_arg)
    tuples, filtering out POSITION and NOOP housekeeping opcodes.

    For CONST-style opcodes the arg is the constant pool index (integer) as
    returned by Code.oparg.  Callers that need the actual constant value should
    call _resolve_arg(code, op, raw_arg) or use resolved_instructions().
    """

    # Opcodes whose arg is a constant-pool index (styles CONST=6, VAR=7)
    _CPOOL_INDEXED_OPS = (
        OpCode.KOS_OP_IMPORT,
        OpCode.KOS_OP_IMPORT_REL,
        OpCode.KOS_OP_IMPORT_FROM,
        OpCode.KOS_OP_CONST,
        OpCode.KOS_OP_VAR,
        OpCode.KOS_OP_VAR_BIND,
        OpCode.KOS_OP_VAR_CLEAR,
        OpCode.KOS_OP_VAR_REF,
        OpCode.KOS_OP_VAR_STORE,
    )

    def _resolve_arg(self, code, op, raw_arg):
        """
        For opcodes whose arg is a constant-pool index, look up the actual
        value.  For other opcodes (e.g. COUNT-style POP) return raw_arg.
        """

        if op in self._CPOOL_INDEXED_OPS:
            return code.constant(raw_arg)
        return raw_arg

    _FILTER_OPS = (
        OpCode.KOS_OP_NOOP,
        OpCode.KOS_OP_POSITION,
        OpCode.KOS_OP_RETURN,
    )

    def instructions(self, code):
        """
        Return list of (op, resolved_arg) for all non-housekeeping instructions.
        Filters out NOOP, POSITION, and RETURN which are infrastructure.
        """

        return [
            (op, self._resolve_arg(code, op, arg))
            for i in range(code.length())
            for op, arg in [code.oparg(i)]
            if op not in self._FILTER_OPS
        ]

    def op_sequence(self, code):
        return [op for op, _ in self.instructions(code)]

    def first_op_with(self, code, opcode):
        for op, arg in self.instructions(code):
            if op == opcode:
                return arg
        return None

    def ops_with(self, code, opcode):
        return [arg for op, arg in self.instructions(code) if op == opcode]


class TestSpecialImportAbsolute(_OpcodeHelper, SpecialsTestCase):

    def test_simple_import_has_import_op(self):
        code = self.compile_to_code('(import os)')
        ops = self.op_sequence(code)
        self.assertIn(OpCode.KOS_OP_IMPORT, ops)

    def test_simple_import_no_import_rel(self):
        code = self.compile_to_code('(import os)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.KOS_OP_IMPORT_REL, ops)

    def test_simple_import_name_arg(self):
        code = self.compile_to_code('(import os)')
        arg = self.first_op_with(code, OpCode.KOS_OP_IMPORT)
        self.assertEqual('os', arg)

    def test_simple_import_var_store(self):
        code = self.compile_to_code('(import os)')
        ops = self.op_sequence(code)
        self.assertIn(OpCode.KOS_OP_VAR_STORE, ops)

    def test_simple_import_binds_leaf_name(self):
        code = self.compile_to_code('(import os)')
        arg = self.first_op_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual('os', arg)

    def test_simple_import_ends_with_const_none(self):
        code = self.compile_to_code('(import os)')
        instrs = self.instructions(code)
        self.assertEqual(OpCode.KOS_OP_CONST, instrs[-1][0])
        self.assertIsNone(instrs[-1][1])

    def test_dotted_import_uses_full_name(self):
        code = self.compile_to_code('(import os.path)')
        arg = self.first_op_with(code, OpCode.KOS_OP_IMPORT)
        self.assertEqual('os.path', arg)

    def test_dotted_import_var_store(self):
        code = self.compile_to_code('(import os.path)')
        arg = self.first_op_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual('path', arg)

    def test_import_with_alias(self):
        code = self.compile_to_code('(import os.path as: p)')
        import_arg = self.first_op_with(code, OpCode.KOS_OP_IMPORT)
        bind_arg = self.first_op_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual('os.path', import_arg)
        self.assertEqual('p', bind_arg)

    def test_import_no_import_from(self):
        code = self.compile_to_code('(import os)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.KOS_OP_IMPORT_FROM, ops)

    def test_import_no_trailing_pop(self):
        code = self.compile_to_code('(import os)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.KOS_OP_POP, ops)


class TestSpecialImportRelative(_OpcodeHelper, SpecialsTestCase):

    def test_single_dot_uses_import_rel(self):
        code = self.compile_to_code('(import .foo)')
        ops = self.op_sequence(code)
        self.assertIn(OpCode.KOS_OP_IMPORT_REL, ops)

    def test_single_dot_no_abs_import(self):
        code = self.compile_to_code('(import .foo)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.KOS_OP_IMPORT, ops)

    def test_single_dot_level_one(self):
        code = self.compile_to_code('(import .foo)')
        level, name = self.first_op_with(code, OpCode.KOS_OP_IMPORT_REL)
        self.assertEqual(1, level)
        self.assertEqual('foo', name)

    def test_double_dot_level_two(self):
        code = self.compile_to_code('(import ..bar)')
        level, name = self.first_op_with(code, OpCode.KOS_OP_IMPORT_REL)
        self.assertEqual(2, level)
        self.assertEqual('bar', name)

    def test_relative_binds_leaf_name(self):
        code = self.compile_to_code('(import .foo)')
        bind_arg = self.first_op_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual('foo', bind_arg)

    def test_relative_dotted_binds_leaf(self):
        code = self.compile_to_code('(import .foo.bar)')
        level, name = self.first_op_with(code, OpCode.KOS_OP_IMPORT_REL)
        bind_arg = self.first_op_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual(1, level)
        self.assertEqual('foo.bar', name)
        self.assertEqual('bar', bind_arg)


class TestSpecialImportFrom(_OpcodeHelper, SpecialsTestCase):

    def test_import_from_has_import_op(self):
        code = self.compile_to_code('(import-from os path)')
        ops = self.op_sequence(code)
        self.assertIn(OpCode.KOS_OP_IMPORT, ops)

    def test_import_from_has_import_from_op(self):
        code = self.compile_to_code('(import-from os path)')
        ops = self.op_sequence(code)
        self.assertIn(OpCode.KOS_OP_IMPORT_FROM, ops)

    def test_import_from_single_member_attr(self):
        code = self.compile_to_code('(import-from os path)')
        arg = self.first_op_with(code, OpCode.KOS_OP_IMPORT_FROM)
        self.assertEqual('path', arg)

    def test_import_from_single_member_bind(self):
        code = self.compile_to_code('(import-from os path)')
        arg = self.first_op_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual('path', arg)

    def test_import_from_multiple_members(self):
        code = self.compile_to_code('(import-from os path sep)')
        from_args = self.ops_with(code, OpCode.KOS_OP_IMPORT_FROM)
        bind_args = self.ops_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual(['path', 'sep'], from_args)
        self.assertEqual(['path', 'sep'], bind_args)

    def test_import_from_has_pop_one(self):
        code = self.compile_to_code('(import-from os path)')
        pop_args = self.ops_with(code, OpCode.KOS_OP_POP)
        self.assertIn(1, pop_args)

    def test_import_from_ends_with_const_none(self):
        code = self.compile_to_code('(import-from os path)')
        instrs = self.instructions(code)
        self.assertEqual(OpCode.KOS_OP_CONST, instrs[-1][0])
        self.assertIsNone(instrs[-1][1])

    def test_import_from_with_alias(self):
        code = self.compile_to_code('(import-from os [getcwd as: cwd])')
        from_args = self.ops_with(code, OpCode.KOS_OP_IMPORT_FROM)
        bind_args = self.ops_with(code, OpCode.KOS_OP_VAR_STORE)
        self.assertEqual(['getcwd'], from_args)
        self.assertEqual(['cwd'], bind_args)

    def test_import_from_order_import_then_from_then_pop(self):
        code = self.compile_to_code('(import-from os path)')
        ops = self.op_sequence(code)
        import_idx = ops.index(OpCode.KOS_OP_IMPORT)
        from_idx = ops.index(OpCode.KOS_OP_IMPORT_FROM)
        pop_idx = ops.index(OpCode.KOS_OP_POP)
        self.assertLess(import_idx, from_idx)
        self.assertLess(from_idx, pop_idx)

    def test_import_from_relative(self):
        code = self.compile_to_code('(import-from .foo bar)')
        ops = self.op_sequence(code)
        self.assertIn(OpCode.KOS_OP_IMPORT_REL, ops)
        self.assertNotIn(OpCode.KOS_OP_IMPORT, ops)
        level, name = self.first_op_with(code, OpCode.KOS_OP_IMPORT_REL)
        self.assertEqual(1, level)
        self.assertEqual('foo', name)

    def test_import_from_module_name_arg(self):
        code = self.compile_to_code('(import-from os path)')
        import_arg = self.first_op_with(code, OpCode.KOS_OP_IMPORT)
        self.assertEqual('os', import_arg)


class TestSpecialImportErrors(_OpcodeHelper, SpecialsTestCase):

    def test_import_no_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import)')

    def test_import_from_no_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import-from)')

    def test_import_from_module_only_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import-from os)')

    def test_import_too_many_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import os path)')

    def test_import_non_symbol_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import 42)')


# The end.
