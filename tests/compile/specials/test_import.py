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
Tests for the import, import-from, and
import-all-from special forms.

These tests inspect the generated opcode sequences without executing them
at the koskari VM level; runtime behavior is exercised by the importer
integration tests in tests/importer/ and tests/lib/opcode/.

Each import special emits one self-contained EXTRA opcode carrying
(name, level, ...) plus the CONST None statement value. The softened
instruction stream resolves EXTRA rows to their 4-tuples, which is what
these tests assert against.
"""

import pytest

from koskari.exceptions import SpecialError
from koskari.lib import OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


_IMPORT_ROOT = 0
_IMPORT_LEAF = 1


class _OpcodeHelper:
    """
    Mixin: extract (OpCode, arg) pairs from a Code object's softened
    instruction stream, filtering housekeeping opcodes. EXTRA opcode args
    appear as resolved 4-tuples of values.
    """

    _FILTER_OPS = (
        OpCode.NOOP,
        OpCode.POSITION,
        OpCode.LABEL,
        OpCode.RETURN,
    )

    def instructions(self, code):
        return [
            (OpCode(op), arg)
            for op, arg in code.soften()
            if OpCode(op) not in self._FILTER_OPS
        ]

    def op_sequence(self, code):
        return [op for op, _ in self.instructions(code)]

    def first_op_with(self, code, opcode):
        for op, arg in self.instructions(code):
            if op == opcode:
                return arg
        return None


class TestSpecialImportAbsolute(_OpcodeHelper, SpecialsTestCase):

    async def test_simple_import_row(self):
        code = self.compile_to_code('(import os)')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('os', 0, _IMPORT_ROOT, 'os'), arg)


    async def test_simple_import_is_self_contained(self):
        code = self.compile_to_code('(import os)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.VAR_STORE, ops)
        self.assertNotIn(OpCode.POP, ops)
        self.assertNotIn(OpCode.IMPORT_FROM, ops)


    async def test_simple_import_ends_with_const_none(self):
        # CONST(None),RETURN(1) is squished into RETURN(0) so we need to
        # fake a CALL(1) to verify the CONST(None) is present

        code = self.compile_to_code('(foo (import os))')
        instrs = self.instructions(code)
        self.assertEqual(OpCode.CONST, instrs[-2][0])
        self.assertEqual(None, instrs[-2][1])


    async def test_dotted_import_binds_root(self):
        code = self.compile_to_code('(import os.path)')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('os.path', 0, _IMPORT_ROOT, 'os'), arg)


    async def test_member_import_binds_leaf_alias(self):
        code = self.compile_to_code('(import [os.path as: p])')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('os.path', 0, _IMPORT_LEAF, 'p'), arg)


    async def test_member_import_undotted(self):
        code = self.compile_to_code('(import [os as: operating-system])')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('os', 0, _IMPORT_LEAF, 'operating-system'), arg)


class TestSpecialImportRelative(_OpcodeHelper, SpecialsTestCase):

    async def test_single_dot_level_one_leaf(self):
        code = self.compile_to_code('(import .foo)')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('foo', 1, _IMPORT_LEAF, 'foo'), arg)


    async def test_double_dot_level_two(self):
        code = self.compile_to_code('(import ..bar)')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('bar', 2, _IMPORT_LEAF, 'bar'), arg)


    async def test_relative_dotted_binds_leaf(self):
        code = self.compile_to_code('(import .foo.bar)')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('foo.bar', 1, _IMPORT_LEAF, 'bar'), arg)


    async def test_relative_member_alias(self):
        code = self.compile_to_code('(import [.foo as: f])')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual(('foo', 1, _IMPORT_LEAF, 'f'), arg)


    async def test_bare_relative_member_alias(self):
        # a lone dot is the reader's dotted-pair marker, so bare
        # relative imports start at two dots
        code = self.compile_to_code('(import [.. as: pkg])')
        arg = self.first_op_with(code, OpCode.IMPORT)
        self.assertEqual((None, 2, _IMPORT_LEAF, 'pkg'), arg)


class TestSpecialImportFrom(_OpcodeHelper, SpecialsTestCase):

    async def test_import_from_single_member(self):
        code = self.compile_to_code('(import-from os path)')
        arg = self.first_op_with(code, OpCode.IMPORT_FROM)
        self.assertEqual(('os', 0, ('path',), ('path',)), arg)


    async def test_import_from_is_self_contained(self):
        code = self.compile_to_code('(import-from os path)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.IMPORT, ops)
        self.assertNotIn(OpCode.VAR_STORE, ops)
        self.assertNotIn(OpCode.POP, ops)


    async def test_import_from_multiple_members(self):
        code = self.compile_to_code('(import-from os path sep)')
        arg = self.first_op_with(code, OpCode.IMPORT_FROM)
        self.assertEqual(('os', 0, ('path', 'sep'), ('path', 'sep')), arg)


    async def test_import_from_with_alias(self):
        code = self.compile_to_code('(import-from os [getcwd as: cwd])')
        arg = self.first_op_with(code, OpCode.IMPORT_FROM)
        self.assertEqual(('os', 0, ('getcwd',), ('cwd',)), arg)


    async def test_import_from_mixed_members(self):
        code = self.compile_to_code(
            '(import-from os.path join [basename as: bn])')
        arg = self.first_op_with(code, OpCode.IMPORT_FROM)
        self.assertEqual(
            ('os.path', 0, ('join', 'basename'), ('join', 'bn')), arg)


    async def test_import_from_relative(self):
        code = self.compile_to_code('(import-from .foo bar)')
        arg = self.first_op_with(code, OpCode.IMPORT_FROM)
        self.assertEqual(('foo', 1, ('bar',), ('bar',)), arg)


    async def test_import_from_bare_relative(self):
        # a lone dot is the reader's dotted-pair marker, so bare
        # relative imports start at two dots
        code = self.compile_to_code('(import-from .. util)')
        arg = self.first_op_with(code, OpCode.IMPORT_FROM)
        self.assertEqual((None, 2, ('util',), ('util',)), arg)


    async def test_import_from_ends_with_const_none(self):
        code = self.compile_to_code('(foo (import-from os path))')
        instrs = self.instructions(code)
        self.assertEqual(OpCode.CONST, instrs[-2][0])
        self.assertEqual(None, instrs[-2][1])


class TestSpecialImportAllFrom(_OpcodeHelper, SpecialsTestCase):

    async def test_import_all_from_row(self):
        code = self.compile_to_code('(import-all-from os)')
        arg = self.first_op_with(code, OpCode.IMPORT_STAR)
        self.assertEqual(('os', 0, 0, 0), arg)


    async def test_import_all_from_dotted(self):
        code = self.compile_to_code('(import-all-from os.path)')
        arg = self.first_op_with(code, OpCode.IMPORT_STAR)
        self.assertEqual(('os.path', 0, 0, 0), arg)


    async def test_import_all_from_relative(self):
        code = self.compile_to_code('(import-all-from .util)')
        arg = self.first_op_with(code, OpCode.IMPORT_STAR)
        self.assertEqual(('util', 1, 0, 0), arg)


    async def test_import_all_from_is_self_contained(self):
        code = self.compile_to_code('(import-all-from os)')
        ops = self.op_sequence(code)
        self.assertNotIn(OpCode.IMPORT, ops)
        self.assertNotIn(OpCode.IMPORT_FROM, ops)
        self.assertNotIn(OpCode.VAR_STORE, ops)
        self.assertNotIn(OpCode.POP, ops)


class TestSpecialImportErrors(_OpcodeHelper, SpecialsTestCase):

    async def test_import_no_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import)')


    async def test_import_inline_as_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import os.path as: p)')


    async def test_import_too_many_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import os path)')


    async def test_import_non_symbol_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import 42)')


    async def test_import_bare_relative_plain_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import ..)')


    async def test_import_from_no_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import-from)')


    async def test_import_from_module_only_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import-from os)')


    async def test_import_all_from_no_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import-all-from)')


    async def test_import_all_from_extra_args_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(import-all-from os extra)')


    async def test_import_all_from_in_lambda_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(lambda [x] (import-all-from os))')


    async def test_import_all_from_in_let_raises(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                '(let [[x 1]] (import-all-from os))')


    def assertNilModuleRefused(self, source):
        """
        A nil module argument is refused as a special error naming the
        import argument, in place of an escaped index error
        """

        with self.assertRaises(SpecialError) as caught:
            self.compile_to_code(source)
        self.assertIn("import argument must be", str(caught.exception))

    async def test_import_nil_module_raises(self):
        self.assertNilModuleRefused('(import ())')

    async def test_import_nil_member_module_raises(self):
        self.assertNilModuleRefused('(import [() as: x])')

    async def test_import_from_nil_module_raises(self):
        self.assertNilModuleRefused('(import-from () x)')

    async def test_import_all_from_nil_module_raises(self):
        self.assertNilModuleRefused('(import-all-from ())')


# The end.
