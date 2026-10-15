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
Tests for the per-context frame recursion limit.

Frame depth is bounded by sys.getrecursionlimit(), read when the
evaluator is entered. Exceeding it raises RecursionError into the
calling frame as an ordinary exception.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


DEEP = """
(begin
  (define f
    (lambda [n]
      (begin
        (call record n)
        (+ 1 (call f (+ n 1))))))
  (call f 1))
"""


class TestRecursionLimit(SpecialsTestCase):

    async def test_deep_recursion_raises(self):
        """
        non-tail recursion past the limit raises RecursionError at the
        Python boundary and stops within the limit
        """

        seen = []
        expr, mod = self.compile_expr(DEEP, record=seen.append)
        with self.assertRaises(RecursionError):
            await expr()
        self.assertLessEqual(seen[-1], sys.getrecursionlimit())
        self.assertGreater(seen[-1], sys.getrecursionlimit() - 20)


    async def test_limit_follows_sys_setrecursionlimit(self):
        """
        the depth reached tracks the Python recursion limit in force
        when evaluation starts
        """

        seen = []
        expr, mod = self.compile_expr(DEEP, record=seen.append)
        saved = sys.getrecursionlimit()
        sys.setrecursionlimit(500)
        try:
            with self.assertRaises(RecursionError):
                await expr()
        finally:
            sys.setrecursionlimit(saved)
        self.assertLessEqual(seen[-1], 500)
        self.assertGreater(seen[-1], 480)


    async def test_recursion_error_is_catchable(self):
        """
        the error raises into the calling frame, so a try around the
        call handles it and evaluation continues
        """

        src = """
        (begin
          (define f
            (lambda [n]
              (+ 1 (call f (+ n 1)))))
          (try
            (call f 1)
            ([RecursionError] (call record "caught")))
          (call record "after"))
        """
        seen = []
        expr, mod = self.compile_expr(
            src, record=seen.append, RecursionError=RecursionError)
        await expr()
        self.assertEqual(seen, ["caught", "after"])


# The end.
