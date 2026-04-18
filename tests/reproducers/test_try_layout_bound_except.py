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
Reproducer: layout try/except/else with [Exception as: e] must compile and run
like the equivalent s-expr (try ...) form.

The compile/try special is correct for proper ((e Exception) HANDLER) clauses.
If layout lowers a bound except to an improper (e . Exception) head, catch
normalisation mis-classifies the clause and behaviour diverges (bad tracebacks,
wrong branch, etc.).
"""

from tests.compile.specials import SpecialsTestCase
from tests.compile.specials.test_try import make_accumulator, make_raise_accumulator

import pytest

pytestmark = pytest.mark.phase_reproducers


class TestTryLayoutBoundExceptElse(SpecialsTestCase):

    async def test_layout_try_except_exception_as_e_else(self):
        """
        Literal layout source: except [Exception as: e] with else: must take
        the except branch (return -111), not else (987), matching
        tests.compile.specials.test_try.TestTrySpecial.test_try_exc_else.
        """
        accu1, bad_guy = make_raise_accumulator()
        accu2, good_guy = make_accumulator()

        src = """
        try:
          (bad_guy 567)
        except [Exception as: e]:
          (good_guy -111)
        else:
          (good_guy 987)
        """

        expr, mod = self.compile_expr(
            src, bad_guy=bad_guy, good_guy=good_guy, Exception=Exception)

        res = await expr()
        self.assertEqual(res, -111)
        self.assertEqual(accu1, [567])
        self.assertEqual(accu2, [-111])
