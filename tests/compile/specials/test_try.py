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


from types import SimpleNamespace

import pytest

from tests.dis.dis import dis

from . import SpecialsTestCase


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
          ([Exception] (good_guy -111)))
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
          ([Exception] (good_guy -111)))
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
          ([Exception] (good_guy -111))
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
          ([Exception] (good_guy -111))
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
          ([KeyError] (good_guy -111))
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
          ([Exception] (good_guy -111))
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
        ``finally`` runs for side effects; try ``return`` value is not
        replaced by a bare finally expression
        """

        src = """
        ((lambda []
          (try
            (return 5)
            [[Exception] (return 9)]
            [finally: 100])))
        """
        expr, mod = self.compile_expr(src)
        res = await expr()
        self.assertEqual(res, 5)


    async def test_try_finally_return_from_non_tail_try(self):
        """
        A ``return`` through ``finally`` carries its value out even when
        the try is not the last form of the lambda
        """

        src = """
        ((lambda []
          (try
            (return 5)
            [finally: 100])
          99))
        """
        expr, mod = self.compile_expr(src)
        res = await expr()
        self.assertEqual(res, 5)


    async def test_try_finally_preserves_compile_return_in_except_when_raised(self):
        """
        When the handler runs, ``return`` from except is not replaced
        by a bare ``finally`` expression
        """

        accu, bad_guy = make_raise_accumulator()

        src = """
        ((lambda []
          (try
            (bad_guy 1)
            [[Exception] (return 9)]
            [finally: 100])))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, 9)
        self.assertEqual(accu, [1])


    async def test_try_finally_compile_return_in_finally_overwrites_try_return(self):
        """
        Explicit ``return`` in ``finally`` replaces a pending try return
        """

        src = """
        ((lambda []
          (try
            (return 5)
            [[Exception] (return 9)]
            [finally: (return 2)])))
        """
        expr, mod = self.compile_expr(src)
        res = await expr()
        self.assertEqual(res, 2)


    async def test_try_finally_compile_return_in_finally_overwrites_except_return(self):
        """
        Explicit ``return`` in ``finally`` replaces a pending except
        return
        """

        accu, bad_guy = make_raise_accumulator()

        src = """
        ((lambda []
          (try
            (bad_guy 1)
            [[Exception] (return 9)]
            [finally: (return -3)])))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, Exception=Exception)
        res = await expr()
        self.assertEqual(res, -3)
        self.assertEqual(accu, [1])


class TestTryCompoundExceptionType(SpecialsTestCase):
    """
    An exception-type expression that is a compound form must be
    compiled as a single expression.

    The reader lowers a dotted atom to (attr obj name), so a
    pair in the type position is the ordinary case rather than an
    exotic one, and a clause naming `mod.Err` must resolve `mod.Err` --
    never the trailing segment on its own.
    """

    async def test_dotted_type_catches(self):
        ns = SimpleNamespace(Err=KeyError)
        accu1, bad_guy = make_raise_accumulator(KeyError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((ns.Err) (good_guy -111)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, ns=ns)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])


    async def test_dotted_type_misses(self):
        ns = SimpleNamespace(Err=KeyError)
        accu1, bad_guy = make_raise_accumulator(ValueError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((ns.Err) (good_guy -111)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, ns=ns)
        with self.assertRaises(ValueError):
            await expr()
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [])


    async def test_dotted_type_ignores_leaf_shadow(self):
        """
        A binding that happens to share the dotted name's last segment
        is unrelated to the clause and must not be consulted.
        """

        ns = SimpleNamespace(Err=KeyError)
        accu1, bad_guy = make_raise_accumulator(ValueError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((ns.Err) (good_guy -111))
          ((ValueError) (good_guy -222)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, ns=ns,
            Err=ValueError, ValueError=ValueError)
        res = await expr()
        self.assertEqual(res, -222)
        self.assertEqual(accu2, [-222])


    async def test_bound_dotted_type(self):
        ns = SimpleNamespace(Err=KeyError)
        accu1, bad_guy = make_raise_accumulator(KeyError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((ns.Err as: e) (good_guy e)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, ns=ns)
        res = await expr()
        self.assertIsInstance(res, KeyError)
        self.assertEqual(accu1, [567])


    async def test_called_type_expression(self):
        def pick():
            return KeyError

        accu1, bad_guy = make_raise_accumulator(KeyError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          (((pick)) (good_guy -111)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, pick=pick)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu2, [-111])


    async def test_tuple_type_expression(self):
        """
        A tuple in the type position is Python's `except (A, B)` -- one
        expression naming several types.
        """

        accu1, bad_guy = make_raise_accumulator(KeyError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          ((#(ValueError KeyError)) (good_guy -111)))
        """
        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy,
            ValueError=ValueError, KeyError=KeyError)
        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu2, [-111])


# The end.
