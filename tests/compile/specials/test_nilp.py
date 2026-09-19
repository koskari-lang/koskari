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
Tests for the nil? special: NILP emission and CHECK_NIL squash.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from koskari.lib import OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestCompileNilp(SpecialsTestCase):
    """nil? emits NILP, which compression lowers to CHECK_NIL"""

    def opcodes(self, code):
        """
        The flat list of opcodes emitted in code.
        """

        return [code.oparg(i)[0] for i in range(code.length())]

    def test_nilp_emits_nilp(self):
        """a bare (nil? x) emits the NILP opcode"""

        code = self.compile_to_code("(nil? x)")
        ops = self.opcodes(code)
        self.assertIn(OpCode.NILP, ops)
        self.assertNotIn(OpCode.CHECK_NIL, ops)

    def test_cond_nilp_squashes_to_check_nil(self):
        """a nil? test in cond lowers NILP + JUMP_FALSE into CHECK_NIL"""

        code = self.compile_to_code("(cond [(nil? x) 1] [else: 2])")
        ops = self.opcodes(code)
        self.assertIn(OpCode.CHECK_NIL, ops)
        # the squash consumes the standalone NILP and the JUMP_FALSE
        self.assertNotIn(OpCode.NILP, ops)


class TestEvalCheckNil(SpecialsTestCase):
    """the fused CHECK_NIL branch takes the right arm at evaluation"""

    async def eval_cond_nilp(self, src_str, **defns):
        """
        Evaluate a cond whose test is a bare nil? and return the arm taken
        """

        expr, mod = self.compile_expr(src_str, **defns)
        return await expr()


    async def test_cond_nilp_pair_takes_false_arm(self):
        """(cond [(nil? x) 0] [else: 1]) yields 1 when x is a pair"""

        result = await self.eval_cond_nilp("(cond [(nil? '(a b c)) 0] [else: 1])")
        self.assertEqual(result, 1)


    async def test_cond_nilp_python_list_takes_false_arm(self):
        """(cond [(nil? x) 0] [else: 1]) yields 1 when x is a Python list"""

        result = await self.eval_cond_nilp("(cond [(nil? #[1 2]) 0] [else: 1])")
        self.assertEqual(result, 1)


    async def test_cond_nilp_bound_python_list_takes_false_arm(self):
        """(cond [(nil? x) 0] [else: 1]) yields 1 when x is bound to a Python list"""

        result = await self.eval_cond_nilp("(cond [(nil? xs) 0] [else: 1])", xs=[1, 2])
        self.assertEqual(result, 1)


    async def test_cond_nilp_nil_takes_true_arm(self):
        """(cond [(nil? x) 0] [else: 1]) yields 0 when x is nil"""

        result = await self.eval_cond_nilp("(cond [(nil? nil) 0] [else: 1])")
        self.assertEqual(result, 0)


    async def test_cond_nilp_quoted_empty_takes_true_arm(self):
        """(cond [(nil? x) 0] [else: 1]) yields 0 when x is a quoted empty list"""

        result = await self.eval_cond_nilp("(cond [(nil? '()) 0] [else: 1])")
        self.assertEqual(result, 0)


    async def test_cond_nilp_bracket_nil_takes_true_arm(self):
        """(cond [(nil? x) 0] [else: 1]) yields 0 when x is bracket nil"""

        result = await self.eval_cond_nilp("(cond [(nil? '[]) 0] [else: 1])")
        self.assertEqual(result, 0)


    async def test_cond_nilp_is_fused(self):
        """the cond forms under test compile through CHECK_NIL, not NILP"""

        for src_str in (
                "(cond [(nil? '(a b c)) 0] [else: 1])",
                "(cond [(nil? #[1 2]) 0] [else: 1])",
                "(cond [(nil? nil) 0] [else: 1])",
                "(cond [(nil? '[]) 0] [else: 1])"):
            code = self.compile_to_code(src_str)
            ops = [code.oparg(i)[0] for i in range(code.length())]
            self.assertIn(OpCode.CHECK_NIL, ops, src_str)
            self.assertNotIn(OpCode.NILP, ops, src_str)


# The end.
