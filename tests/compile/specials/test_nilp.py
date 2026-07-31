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
Tests for the compile/nil? special: NILP emission and CHECK_NIL squash.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from . import SpecialsTestCase

from koskari.lib import OpCode

import pytest

pytestmark = pytest.mark.phase_specials


class TestCompileNilp(SpecialsTestCase):
    """compile/nil? emits NILP, which compression lowers to CHECK_NIL"""

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


# The end.
