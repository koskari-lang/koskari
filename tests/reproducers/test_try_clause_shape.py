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
Reproducer: a try form whose clause position holds a call is rejected
at compile time.

(try BODY CLAUSE...) takes one BODY expression. A second body form
written before the clauses has the shape of a type-only clause, (HEAD
FORMS...), and the special once accepted a bare symbol as a clause
head, so (try (a) (print "x") [[Exception as: e] (h)]) compiled to a
handler for the exception type print and silently skipped the print.
The clause head must be except:, else:, finally:, or a bracketed using
cell, and the arms must come in that order.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from koskari.exceptions import SyntaxMatchError
from tests.compile.specials import SpecialsTestCase
from tests.compile.specials.test_try import (
    make_accumulator, make_raise_accumulator)


pytestmark = pytest.mark.phase_reproducers


class TestTryClauseShape(SpecialsTestCase):

    async def test_second_body_form_is_rejected(self):
        src = """
        (try
          (good_guy 1)
          (good_guy 2)
          [[Exception as: e] (good_guy -111)])
        """
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code(src)


    async def test_body_form_between_arms_is_rejected(self):
        src = """
        (try
          (good_guy 1)
          [[KeyError] (good_guy -111)]
          (good_guy 2)
          [finally: (good_guy 999)])
        """
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code(src)


    async def test_bare_symbol_head_is_rejected(self):
        src = """
        (try
          (good_guy 1)
          (Exception (good_guy -111)))
        """
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code(src)


    async def test_arm_after_else_is_rejected(self):
        src = """
        (try
          (good_guy 1)
          [else: (good_guy 888)]
          [[Exception] (good_guy -111)])
        """
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code(src)


    async def test_finally_before_else_is_rejected(self):
        src = """
        (try
          (good_guy 1)
          [finally: (good_guy 999)]
          [else: (good_guy 888)])
        """
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code(src)


    async def test_bracketed_arms_in_order_compile_and_run(self):
        accu1, bad_guy = make_raise_accumulator(KeyError)
        accu2, good_guy = make_accumulator()

        src = """
        (try
          (bad_guy 567)
          [[ValueError] (good_guy -1)]
          [[KeyError as: e] (good_guy -111)]
          [else: (good_guy 888)]
          [finally: (good_guy 999)])
        """
        expr, _mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy,
            ValueError=ValueError, KeyError=KeyError)
        self.assertEqual(await expr(), -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111, 999])


# The end.
