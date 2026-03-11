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
Tests for koskari.compile #bytecode special form.
"""


from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestBytecodeSpecial(SpecialsTestCase):

    async def test_bytecode_const_return(self):
        src_str = """
        (bytecode
          [CONST "Hello World"]
          [RETURN 1]
        )
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual("Hello World", result)


    async def test_bytecode_var_call(self):
        src_str = """
        (bytecode
          [VAR add]
          [VAR value_a]
          [VAR value_b]
          [CALL 2]
          [RETURN 1]
        )
        """
        expr, mod = self.compile_expr(
            src_str,
            add=lambda a, b: a + b,
            value_a="Hello",
            value_b=" World",
        )
        result = await expr()
        self.assertEqual("Hello World", result)


# The end.
