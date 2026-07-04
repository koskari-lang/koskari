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
Tests for koskari.compile try/except/finally special form.
"""


from . import SpecialsTestCase
from koskari.dis import dis

import pytest

pytestmark = pytest.mark.phase_specials


def make_accumulator():
    accu = []

    def accumulate(x):
        accu.append(x)
        return x

    return accu, accumulate


def make_raise_accumulator(excclass=Exception):
    accu = []

    def accumulate(x):
        accu.append(x)
        raise excclass(x)

    return accu, accumulate


class TestTrySpecial(SpecialsTestCase):

    async def test_try_noexc(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = await expr()
        self.assertEqual(res, 567)
        self.assertEqual(accu, [567])


    async def test_try_noexc_else(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567)
          (else: (good_guy 888)))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = await expr()
        self.assertEqual(res, 888)
        self.assertEqual(accu, [567, 888])


    async def test_try_noexc_finally(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567)
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = await expr()
        self.assertEqual(res, 567)
        self.assertEqual(accu, [567, 999])


    async def test_try_noexc_else_finally(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567)
          (else: (good_guy 888))
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = await expr()
        self.assertEqual(res, 888)
        self.assertEqual(accu, [567, 888, 999])


    async def test_try_nocatch(self):
        accu, bad_guy = make_raise_accumulator()

        src = """
        (try
          (bad_guy 567))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy)
        with self.assertRaises(Exception):
            await expr()
        self.assertEqual(accu, [567])


    async def test_try_exc(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)

        dis(expr.code)

        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    async def test_bare_except(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (except: (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    async def test_bare_except_catches_base(self):
        accu1, bad_guy = make_raise_accumulator(BaseException)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 999)
          (except: (good_guy -222)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy)
        res = await expr()
        self.assertEqual(res, -222)
        self.assertEqual(accu1, [999])
        self.assertEqual(accu2, [-222])


    async def test_named_try_exc(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((e Exception) (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    async def test_try_exc_miss(self):
        accu1, bad_guy = make_raise_accumulator(BaseException)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 123)
          (Exception (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        with self.assertRaises(BaseException):
            await expr()
        self.assertEqual(accu1, [123])
        self.assertEqual(accu2, [])


    async def test_try_exc_else(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111))
          (else: (good_guy 987)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    async def test_try_exc_finally(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111))
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111, 999])


    async def test_try_nocatch_finally(self):
        accu1, bad_guy = make_raise_accumulator(ValueError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy)
        with self.assertRaises(ValueError):
            await expr()
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [999])


    async def test_try_exc_miss_finally(self):
        accu1, bad_guy = make_raise_accumulator(ValueError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (KeyError (good_guy -111))
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, KeyError=KeyError)
        with self.assertRaises(ValueError):
            await expr()
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [999])


    async def test_try_exc_else_finally(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111))
          (else: (good_guy 987))
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111, 999])


    async def test_try_finally_preserves_compile_return_in_try(self):
        """
        ``finally`` runs for side effects; try ``compile/return`` value is not
        replaced by a bare finally expression
        """

        src = """
        ((lambda []
          (try
            (compile/return 5)
            [[Exception] (compile/return 9)]
            [finally: 100])))
        """
        expr, mod = self.compile_expr(src)
        res = await expr()
        self.assertEqual(res, 5)


    async def test_try_finally_preserves_compile_return_in_except_when_raised(self):
        """
        When the handler runs, ``compile/return`` from except is not replaced
        by a bare ``finally`` expression
        """

        accu, bad_guy = make_raise_accumulator()

        src = """
        ((lambda []
          (try
            (bad_guy 1)
            [[Exception] (compile/return 9)]
            [finally: 100])))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, 9)
        self.assertEqual(accu, [1])


    async def test_try_finally_compile_return_in_finally_overwrites_try_return(self):
        """
        Explicit ``compile/return`` in ``finally`` replaces a pending try return
        """

        src = """
        ((lambda []
          (try
            (compile/return 5)
            [[Exception] (compile/return 9)]
            [finally: (compile/return 2)])))
        """
        expr, mod = self.compile_expr(src)
        res = await expr()
        self.assertEqual(res, 2)


    async def test_try_finally_compile_return_in_finally_overwrites_except_return(self):
        """
        Explicit ``compile/return`` in ``finally`` replaces a pending except
        return
        """

        accu, bad_guy = make_raise_accumulator()

        src = """
        ((lambda []
          (try
            (bad_guy 1)
            [[Exception] (compile/return 9)]
            [finally: (compile/return -3)])))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, -3)
        self.assertEqual(accu, [1])


# The end.
