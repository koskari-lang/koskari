# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for compile/call special: tail position emits TAIL_CALL; non-tail uses CALL
"""


from koskari.lib import OpCode

from . import SpecialsTestCase


class TestCallSpecialTailPosition(SpecialsTestCase):
    """
    compile/call in tail position emits TAIL_CALL; otherwise CALL
    """

    def test_lambda_body_tail_call_one_arg(self):
        code = self.compile_to_code("(lambda [x] (f x))", tc=True)
        ops = self.call_ops(self.lambda_body_code(code))
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)

    def test_lambda_body_tail_call_zero_args(self):
        code = self.compile_to_code("(lambda [] (f))", tc=True)
        ops = self.call_ops(self.lambda_body_code(code))
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 0)

    def test_lambda_begin_first_call_then_tail_call(self):
        code = self.compile_to_code("(lambda [x] (begin (f 1) (g x)))", tc=True)
        ops = self.call_ops(self.lambda_body_code(code))
        self.assertEqual(len(ops), 2)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL)
        self.assertEqual(ops[0][2], 1)
        self.assertEqual(ops[1][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[1][2], 1)

    def test_begin_single_form_tail_call(self):
        code = self.compile_to_code("(begin (f 1))", tc=True)
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)

    def test_begin_f_x_x_call_not_tail(self):
        """
        (begin (f x) x) -> f is not in tail position -> CALL
        """
        code = self.compile_to_code("(begin (f x) x)", tc=True)
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL)
        self.assertEqual(ops[0][2], 1)

    def test_begin_x_f_x_tail_call(self):
        """
        (begin x (f x)) -> f is in tail position -> TAIL_CALL
        """
        code = self.compile_to_code("(begin x (f x))", tc=True)
        ops = self.call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)


# The end.
