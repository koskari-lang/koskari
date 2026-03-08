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
Tests for koskari.compile compile/let special form.
"""


from koskari.dis import dis
from . import SpecialsTestCase


class TestLetSpecial(SpecialsTestCase):

    async def test_let_single_binding(self):
        src_str = "(compile/let [[x 42]] x)"
        expr, mod = self.compile_expr(src_str)

        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


    async def test_let_multiple_bindings(self):
        src_str = """
        (compile/let [[a 1] [b 2]]
          (compile/call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})

        dis(expr.code)

        result = await expr()
        self.assertEqual(3, result)


    async def test_let_body_sequence(self):
        src_str = """
        (compile/let [[x 10]]
          (compile/begin
            (compile/call * x 2)))
        """
        expr, mod = self.compile_expr(src_str, **{"*": lambda x, y: x * y})

        dis(expr.code)

        result = await expr()
        self.assertEqual(20, result)


    async def test_named_let_invoked_lambda(self):
        # (let foo [[a 1] [b 2]] ...body) -> ((lambda foo [a b] ...body) 1 2)
        src_str = """
        (compile/let foo [[a 10] [b 20]]
          (compile/call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})
        result = await expr()
        self.assertEqual(30, result)


# The end.
