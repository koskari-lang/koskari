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
Tests for koskari.compile setq and delq special forms.
"""


import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestSetqSpecial(SpecialsTestCase):

    async def test_setq_then_read(self):
        src_str = """
        (let [[x 0]]
          (begin
            (setq x 42)
            x))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


class TestSetqValuesSpecial(SpecialsTestCase):

    async def test_setq_values_pair_pattern(self):
        src_str = """
        (let [[a 0] [b 0]]
          (begin
            (setq-values [a b] (call pair 1 2))
            (call add a b)))
        """
        expr, mod = self.compile_expr(
            src_str,
            pair=lambda u, v: (u, v),
            add=lambda p, q: p + q)
        result = await expr()
        self.assertEqual(3, result)


    async def test_setq_values_three_element_pattern(self):
        src_str = """
        (let [[a 0] [b 0] [c 0]]
          (begin
            (setq-values [a b c] seq)
            (call triple a b c)))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, 3), result)


    async def test_setq_values_star_element_pattern(self):
        src_str = """
        (let [[a 0] [b 0] [c 0]]
          (begin
            (setq-values [a *: b c] seq)
            (call triple a b c)))
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
        (begin
          (define-values [a b] (call pair 1 2))
          (call add a b))
        """
        expr, mod = self.compile_expr(
            src_str,
            pair=lambda u, v: (u, v),
            add=lambda p, q: p + q)
        result = await expr()
        self.assertEqual(3, result)


    async def test_define_values_three_element_pattern(self):
        src_str = """
        (begin
          (define-values [a b c] seq)
          (call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, 3), result)


    async def test_define_values_nested_front(self):
        src_str = """
        (begin
          (define-values [[a b] c] seq)
          (call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=((1, 2), 3),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, 3), result)


    async def test_define_values_non_iterable_raises(self):
        src_str = """
        (begin
          (define-values [a b c] seq)
          a)
        """
        expr, mod = self.compile_expr(src_str, seq=None)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("non-iterable NoneType", str(ctx.exception))


    async def test_define_values_star_non_iterable_raises(self):
        src_str = """
        (begin
          (define-values [a *: rest] seq)
          a)
        """
        expr, mod = self.compile_expr(src_str, seq=7)
        with self.assertRaises(TypeError) as ctx:
            await expr()
        self.assertIn("non-iterable int", str(ctx.exception))


    async def test_define_values_nested_back(self):
        src_str = """
        (begin
          (define-values [a [b c]] seq)
          (call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, (2, 3)),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, 3), result)


    async def test_define_values_star_element_pattern(self):
        src_str = """
        (begin
          (define-values [a *: b c] seq)
          (call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3, 4, 5, 6),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, (2, 3, 4, 5), 6), result)


    async def test_define_values_star_nested_front(self):
        src_str = """
        (begin
          (define-values [[a b] *: c] seq)
          (call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=((1, 2), 3, 4, 5, 6),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual((1, 2, (3, 4, 5, 6)), result)


    async def test_define_values_star_nested_back(self):
        src_str = """
        (begin
          (define-values [*: a [b c]] seq)
          (call triple a b c))
        """
        expr, mod = self.compile_expr(
            src_str,
            seq=(1, 2, 3, 4, (5, 6)),
            triple=lambda x, y, z: (x, y, z))
        result = await expr()
        self.assertEqual(((1, 2, 3, 4), 5, 6), result)


class TestDelqSpecial(SpecialsTestCase):

    async def test_delq_returns_none(self):
        src_str = """
        (let [[z 1]]
          (delq z))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNone(result)


    async def test_delq_removes(self):
        src_str = """
        (delq z)
        """
        expr, mod = self.compile_expr(src_str, z=1, x=2)
        result = await expr()
        self.assertIsNone(result)

        glbls = vars(mod)
        refx = glbls.ref("x")
        self.assertFalse(refx.empty)

        refz = glbls.ref("z")
        self.assertTrue(refz.empty)


# The end.
