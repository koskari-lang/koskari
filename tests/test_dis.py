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

from koskari import Code, NamespaceSpec, OpCode
from koskari.dis import dis, _get_code, _get_instruction_lines, _format_constant


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


class TestGetCode(unittest.TestCase):

    def test_code_passthrough(self):
        code = _make_truthy_code()
        self.assertIs(_get_code(code), code)

    def test_non_code_returns_none(self):
        self.assertIs(_get_code(42), None)
        self.assertIs(_get_code(type('NoCode', (), {})()), None)

    def test_object_with_code_attr_not_accepted(self):
        """
        Only Code is accepted; .code attribute is not resolved
        """
        code = _make_truthy_code()
        wrapper = type('Wrapper', (), {'code': code})()
        self.assertIs(_get_code(wrapper), None)
        with self.assertRaises(TypeError):
            dis(wrapper)

    def test_non_code_raises_from_dis(self):
        from koskari.dis import dis
        with self.assertRaises(TypeError):
            dis(42)


class TestDisOutput(unittest.TestCase):

    def test_dis_truthy_yus(self):
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

    def test_dis_with_locals_and_closures(self):
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


class TestGetInstructionLines(unittest.TestCase):

    def test_instruction_lines_match_oparg(self):
        code = _make_truthy_code()
        lines = list(_get_instruction_lines(code))
        self.assertEqual(len(lines), 6)
        self.assertEqual(lines[0][0], 0)
        self.assertEqual(lines[0][1], 'KOS_OP_VAR')
        self.assertIn('0 (', lines[0][2])
        self.assertIn('YUS', lines[0][2])
        self.assertEqual(lines[1][1], 'KOS_OP_JUMP_TRUE')
        self.assertIn('(-> 4)', lines[1][2])


class TestFormatConstant(unittest.TestCase):

    def test_short_repr_unchanged(self):
        self.assertEqual(_format_constant(42), '42')
        self.assertEqual(_format_constant('x'), "'x'")

    def test_long_repr_truncated(self):
        long_str = 'x' * 70
        out = _format_constant(long_str)
        self.assertEqual(len(out), 60)
        self.assertTrue(out.endswith('...'))


# The end.
