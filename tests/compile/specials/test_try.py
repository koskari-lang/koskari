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

    def test_try_noexc(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = expr().run()
        self.assertEqual(res, 567)
        self.assertEqual(accu, [567])


    def test_try_noexc_else(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567)
          (else: (good_guy 888)))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = expr().run()
        self.assertEqual(res, 888)
        self.assertEqual(accu, [567, 888])


    def test_try_noexc_finally(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567)
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = expr().run()
        self.assertEqual(res, 999)
        self.assertEqual(accu, [567, 999])


    def test_try_noexc_else_finally(self):
        accu, good_guy = make_accumulator()

        src = """
        (try
          (good_guy 567)
          (else: (good_guy 888))
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, good_guy=good_guy)
        res = expr().run()
        self.assertEqual(res, 999)
        self.assertEqual(accu, [567, 888, 999])


    def test_try_nocatch(self):
        accu, bad_guy = make_raise_accumulator()

        src = """
        (try
          (bad_guy 567))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy)
        with self.assertRaises(Exception):
            expr().run()
        self.assertEqual(accu, [567])


    def test_try_exc(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)

        dis(expr.code)

        res = expr().run()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    def test_bare_except(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (except: (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy)
        res = expr().run()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    def test_bare_except_catches_base(self):
        accu1, bad_guy = make_raise_accumulator(BaseException)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 999)
          (except: (good_guy -222)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy)
        res = expr().run()
        self.assertEqual(res, -222)
        self.assertEqual(accu1, [999])
        self.assertEqual(accu2, [-222])


    def test_named_try_exc(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((e Exception) (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = expr().run()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    def test_try_exc_miss(self):
        accu1, bad_guy = make_raise_accumulator(BaseException)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 123)
          (Exception (good_guy -111)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        with self.assertRaises(BaseException):
            expr().run()
        self.assertEqual(accu1, [123])
        self.assertEqual(accu2, [])


    def test_try_exc_else(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111))
          (else: (good_guy 987)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = expr().run()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    def test_try_exc_finally(self):
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (Exception (good_guy -111))
          (finally: (good_guy 999)))
        """
        expr, mod = self.compile_expr(src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)
        res = expr().run()
        self.assertEqual(res, 999)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111, 999])


    def test_try_exc_else_finally(self):
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
        res = expr().run()
        self.assertEqual(res, 999)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111, 999])


# The end.
