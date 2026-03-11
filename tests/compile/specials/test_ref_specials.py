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
Tests for koskari.compile compile/deref special form.

Tests for the compile/ref macro (ref symbol, ref attr, ref item, &expr) live
in tests.importer.test_basics (phase_basics) since they require basics.ki.
"""


from koskari.lib import OpCode, deref

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestDerefSpecial(SpecialsTestCase):

    async def test_deref_emits_deref_opcode(self):
        src_str = """
        (compile/let [[x 42]]
          (compile/let [[r (compile/refq x)]]
            (compile/deref r)))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


    async def test_deref_uses_deref_opcode(self):
        code = self.compile_to_code("""
        (compile/let [[x 1]]
          (compile/let [[r (compile/refq x)]]
            (compile/deref r)))
        """)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.KOS_OP_DEREF, ops)


    async def test_deref_call_for_higher_order(self):
        src_str = """
        (compile/let [[a 1] [b 2]]
          (compile/let [[ra (compile/refq a)] [rb (compile/refq b)]]
            (compile/call deref ra)))
        """
        expr, mod = self.compile_expr(src_str, deref=deref)
        result = await expr()
        self.assertEqual(1, result)


# The end.
