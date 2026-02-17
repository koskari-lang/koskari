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
Tests for koskari.compile compile/quote special form.
"""


from koskari.lib import cons, symbol

from . import SpecialsTestCase


class TestQuoteSpecial(SpecialsTestCase):

    def test_quote_atom_number(self):
        src_str = "(compile/quote 42)"
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(42, result)


    def test_quote_atom_symbol(self):
        src_str = "(compile/quote x)"
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, symbol("x"))


    def test_quote_list_expands_to_cons(self):
        """
        (compile/quote (1 2 3)) is rewritten to (cons (compile/quote 1) ...)
        and compiles to a call to cons. Pass cons from koskari.lib so the
        result is (1 . (2 . (3 . nil))); unpack to get [1, 2, 3].
        """
        src_str = "(compile/quote (1 2 3))"
        expr, mod = self.compile_expr(src_str, cons=cons)
        result = expr().run()
        self.assertIsNotNone(result)
        self.assertEqual([1, 2, 3], list(result.unpack()))


# The end.
