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
Reproducer: corpus entry special-layout-try-named-002 triggers a segfault on
Python 3.11 during eval of a layout try/except with a named binding and type.

Source under test:

    try:
      (/ 1 0)
    except [e ZeroDivisionError]:
      (str e)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


class TestCorpusTryNamedExcept(SpecialsTestCase):

    async def test_layout_try_named_except_binding(self):
        """
        Corpus entry special-layout-try-named-002: layout try with named
        except binding [e ZeroDivisionError] must catch and bind the exception,
        returning (str e) = "division by zero".

        Segfaults on Python 3.11 during eval.
        """

        src = """
        try:
          (/ 1 0)
        except [e ZeroDivisionError]:
          (str e)
        """

        expr, _mod = self.compile_expr(
            src, ZeroDivisionError=ZeroDivisionError, str=str)

        result = await expr()
        self.assertEqual(result, "division by zero")


# The end.
