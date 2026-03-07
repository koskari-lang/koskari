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
Tests for koskari.compile compile/for-each.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from . import SpecialsTestCase


def _append(seq, x):
    seq.append(x)
    return None


class TestForEachSpecial(SpecialsTestCase):

    async def test_foreach_basic(self):
        col = []
        src = """
        (compile/for-each [i x]
          (compile/call append col (compile/call add 100 i)))
        """
        expr, mod = self.compile_expr(
            src, x=range(0, 5), col=col,
            append=_append, add=lambda a, b: a + b)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(col, [100, 101, 102, 103, 104])


    async def test_foreach_unused_else(self):
        col = []
        src = """
        (compile/for-each [i x]
          (compile/call append col (compile/call add 100 i))
          [else: None])
        """
        expr, mod = self.compile_expr(
            src, x=range(0, 5), col=col,
            append=_append, add=lambda a, b: a + b)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(col, [100, 101, 102, 103, 104])


    async def test_foreach_else_fires_on_exhaustion(self):
        col = []
        src = """
        (compile/for-each [i x]
          (compile/cond
            [(compile/call gt i 4) (compile/break)]
            [else: (compile/call append col (compile/call add 100 i))])
          [else: "unmatched"])
        """
        expr, mod = self.compile_expr(
            src, x=range(0, 4), col=col,
            gt=lambda a, b: a > b,
            append=_append, add=lambda a, b: a + b)
        result = await expr()
        self.assertEqual(result, "unmatched")
        self.assertEqual(col, [100, 101, 102, 103])


    async def test_foreach_else_skipped_on_break(self):
        col = []
        src = """
        (compile/for-each [i x]
          (compile/cond
            [(compile/call gt i 4) (compile/break)]
            [else: (compile/call append col (compile/call add 100 i))])
          [else: "unmatched"])
        """
        expr, mod = self.compile_expr(
            src, x=range(0, 10), col=col,
            gt=lambda a, b: a > b,
            append=_append, add=lambda a, b: a + b)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(col, [100, 101, 102, 103, 104])


    async def test_foreach_empty_iterable_returns_none(self):
        col = []
        src = """
        (compile/for-each [i x]
          (compile/call append col i))
        """
        expr, mod = self.compile_expr(
            src, x=[], col=col, append=_append)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(col, [])


    async def test_foreach_break_with_value(self):
        src = """
        (compile/for-each [i x]
          (compile/cond
            [(compile/call gt i 2) (compile/break "found")]
            [else: None]))
        """
        expr, mod = self.compile_expr(
            src, x=range(0, 10), gt=lambda a, b: a > b)
        result = await expr()
        self.assertEqual(result, "found")


# The end.
