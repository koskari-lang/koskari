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
Tests for koskari.compile while, break, continue.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import operator

import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def append(seq, x):
    seq.append(x)
    return None


class TestWhileSpecial(SpecialsTestCase):

    async def test_while_simple(self):
        c = []
        src_str = """
        (while x
          (call append c x)
          (setq x (call sub x 1)))
        """
        expr, mod = self.compile_expr(src_str, x=5, c=c, append=append,
                                      sub=operator.sub)
        await expr()
        self.assertEqual(c, [5, 4, 3, 2, 1])
        self.assertEqual(mod.x, 0)


    async def test_while_false_never_runs(self):
        trigger_called = []

        def trigger():
            trigger_called.append(True)

        src_str = "(while False (call trigger))"
        expr, mod = self.compile_expr(src_str, trigger=trigger)
        await expr()
        self.assertEqual(trigger_called, [])


    async def test_while_break(self):
        c = []
        src_str = """
        (while True
          (cond
            [x (call append c x)
               (setq x (call sub x 1))]
            [else: (break "done")]))
        """
        expr, mod = self.compile_expr(src_str, x=5, c=c, append=append,
                                      sub=operator.sub)
        result = await expr()
        self.assertEqual(result, "done")
        self.assertEqual(c, [5, 4, 3, 2, 1])


    async def test_while_break_value(self):
        src_str = "(while True (break 99))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 99)


    async def test_while_continue(self):
        c = []
        src_str = """
        (while x
          (cond [(call odd x)
                         (setq x (call sub x 1))
                         (continue)])
          (call append c x)
          (setq x (call sub x 1)))
        """
        expr, mod = self.compile_expr(
            src_str, x=11, c=c, append=append, sub=operator.sub,
            odd=lambda n: n % 2 == 1)
        await expr()
        self.assertEqual(c, [10, 8, 6, 4, 2])
        self.assertEqual(mod.x, 0)


    async def test_while_else_multiple_forms(self):
        """
        every form after the else marker runs, in order, and the last
        one is the loop's value
        """

        c = []
        src_str = """
        (while x
          (call append c x)
          (setq x (call sub x 1))
          [else: (call append c "first") (call append c "second") "done"])
        """
        expr, mod = self.compile_expr(
            src_str, x=2, c=c, append=append, sub=operator.sub)
        result = await expr()
        self.assertEqual(result, "done")
        self.assertEqual(c, [2, 1, "first", "second"])


# The end.
