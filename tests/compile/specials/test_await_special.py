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
Tests for compile/await special
"""


from koskari.exceptions import SpecialError
from koskari.lib import OpCode

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


def _opcodes(code):
    return [code.oparg(i)[0] for i in range(code.length())]


class TestCompileAwait(SpecialsTestCase):

    async def test_await_emits_opcode(self):
        code = self.compile_to_code("(compile/await (compile/quote 1))")
        ops = _opcodes(code)
        self.assertIn(OpCode.KOS_OP_AWAIT, ops)
        aw = ops.index(OpCode.KOS_OP_AWAIT)
        ret = ops.index(OpCode.KOS_OP_RETURN)
        self.assertLess(aw, ret)
        self.assertEqual(ops[aw - 1], OpCode.KOS_OP_CONST)

    async def test_await_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/await)")

        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/await a b)")


# The end.
