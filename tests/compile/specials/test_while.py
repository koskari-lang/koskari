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
Tests for koskari.compile compile/while, compile/break, compile/continue.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import operator

from . import SpecialsTestCase


def append(seq, x):
    seq.append(x)
    return None


class TestWhileSpecial(SpecialsTestCase):

    def test_while_simple(self):
        c = []
        src_str = """
        (compile/while x
          (compile/call append c x)
          (compile/setq x (compile/call sub x 1)))
        """
        expr, mod = self.compile_expr(src_str, x=5, c=c, append=append,
                                      sub=operator.sub)
        expr().run()
        self.assertEqual(c, [5, 4, 3, 2, 1])
        self.assertEqual(mod.x, 0)


    def test_while_false_never_runs(self):
        trigger_called = []

        def trigger():
            trigger_called.append(True)

        src_str = "(compile/while False (compile/call trigger))"
        expr, mod = self.compile_expr(src_str, trigger=trigger)
        expr().run()
        self.assertEqual(trigger_called, [])


    def test_while_break(self):
        c = []
        src_str = """
        (compile/while True
          (compile/cond
            [x (compile/call append c x)
               (compile/setq x (compile/call sub x 1))]
            [else: (compile/break "done")]))
        """
        expr, mod = self.compile_expr(src_str, x=5, c=c, append=append,
                                      sub=operator.sub)
        result = expr().run()
        self.assertEqual(result, "done")
        self.assertEqual(c, [5, 4, 3, 2, 1])


    def test_while_break_value(self):
        src_str = "(compile/while True (compile/break 99))"
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, 99)


    def test_while_continue(self):
        c = []
        src_str = """
        (compile/while x
          (compile/cond [(compile/call odd x)
                         (compile/setq x (compile/call sub x 1))
                         (compile/continue)])
          (compile/call append c x)
          (compile/setq x (compile/call sub x 1)))
        """
        expr, mod = self.compile_expr(
            src_str, x=11, c=c, append=append, sub=operator.sub,
            odd=lambda n: n % 2 == 1)
        expr().run()
        self.assertEqual(c, [10, 8, 6, 4, 2])
        self.assertEqual(mod.x, 0)


# The end.
