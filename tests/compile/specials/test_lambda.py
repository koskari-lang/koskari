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
Tests for koskari.compile lambda special (named and unnamed).
"""


from . import SpecialsTestCase


class TestLambdaAnonymous(SpecialsTestCase):

    async def test_anonymous_lambda_identity(self):
        src = "(lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        result = fn(42).run()
        self.assertEqual(result, 42)


    async def test_anonymous_lambda_repr_has_debug_label(self):
        src = "(lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        repr_str = repr(fn)
        self.assertIn("<lambda>", repr_str,
                      "anonymous lambda repr should show debug label")


class TestLambdaNamed(SpecialsTestCase):

    async def test_named_lambda_repr_shows_name(self):
        src = "(lambda fact [n] n)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        repr_str = repr(fn)
        self.assertIn("fact", repr_str,
                      "named lambda repr should show proper name")


    async def test_named_lambda_identity(self):
        src = "(lambda fact [n] n)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        result = await fn(10)
        self.assertEqual(result, 10)


# The end.
