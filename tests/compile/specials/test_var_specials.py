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
Tests for koskari.compile compile/setq and compile/delq special forms.
"""


from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestSetqSpecial(SpecialsTestCase):

    async def test_setq_then_read(self):
        src_str = """
        (compile/let [[x 0]]
          (compile/begin
            (compile/setq x 42)
            x))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


class TestSetqValuesSpecial(SpecialsTestCase):

    async def test_setq_values_pair_pattern(self):
        src_str = """
        (compile/let [[a 0] [b 0]]
          (compile/begin
            (compile/setq-values [a b] (compile/call pair 1 2))
            (compile/call add a b)))
        """
        expr, mod = self.compile_expr(
            src_str,
            pair=lambda u, v: (u, v),
            add=lambda p, q: p + q)
        result = await expr()
        self.assertEqual(3, result)

    async def test_setq_values_three_element_pattern(self):
        src_str = """
        (compile/let [[a 0] [b 0] [c 0]]
          (compile/begin
            (compile/setq-values [a b c] seq)
            (compile/call triple a b c)))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, 3), result)

    async def test_setq_values_star_element_pattern(self):
        src_str = """
        (compile/let [[a 0] [b 0] [c 0]]
          (compile/begin
            (compile/setq-values [a *: b c] seq)
            (compile/call triple a b c)))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3, 4, 5, 6),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, (2, 3, 4, 5), 6), result)


class TestDefineValuesSpecial(SpecialsTestCase):

    async def test_define_values_pair_pattern(self):
        src_str = """
        (compile/begin
          (compile/define-values [a b] (compile/call pair 1 2))
          (compile/call add a b))
        """
        expr, mod = self.compile_expr(
            src_str,
            pair=lambda u, v: (u, v),
            add=lambda p, q: p + q)
        result = await expr()
        self.assertEqual(3, result)

    async def test_define_values_three_element_pattern(self):
        src_str = """
        (compile/begin
          (compile/define-values [a b c] seq)
          (compile/call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, 3), result)

    async def test_define_values_star_element_pattern(self):
        src_str = """
        (compile/begin
          (compile/define-values [a *: b c] seq)
          (compile/call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3, 4, 5, 6),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, (2, 3, 4, 5), 6), result)

class TestDelqSpecial(SpecialsTestCase):

    async def test_delq_returns_none(self):
        src_str = """
        (compile/let [[z 1]]
          (compile/delq z))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNone(result)


# The end.
