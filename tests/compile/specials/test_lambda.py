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

import pytest

pytestmark = pytest.mark.phase_specials


class TestLambdaAnonymous(SpecialsTestCase):

    async def test_anonymous_lambda_identity(self):
        src = "(lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        result = await fn(42)
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


    async def test_lambda_docstring_from_leading_str(self):
        src = '(lambda [x] "module doc line" x)'
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(fn.__doc__, 'module doc line')
        self.assertEqual(fn.code.extras.get('doc'), 'module doc line')

    async def test_lambda_param_annotation_real_object(self):
        src = "(lambda [(a :: int)] a)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertEqual(fn.__annotations__, {"a": int})
        self.assertIs(fn.__annotations__["a"], int)

    async def test_lambda_param_annotation_string_forward_ref(self):
        src = '(lambda [(a :: "int")] a)'
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__["a"], int)

    async def test_lambda_param_annotation_unresolved_string(self):
        src = '(lambda [(a :: "NoSuchThing")] a)'
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(fn.__annotations__, {"a": "NoSuchThing"})


class TestLambdaReturnType(SpecialsTestCase):

    async def test_named_lambda_double_colon_return(self):
        src = "(lambda f :: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)
        self.assertEqual(await fn(7), 7)

    async def test_named_lambda_type_kw_return(self):
        src = "(lambda f type: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)

    async def test_anonymous_lambda_double_colon_return(self):
        src = "(lambda :: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)
        self.assertEqual(await fn(3), 3)

    async def test_anonymous_lambda_type_kw_return(self):
        src = "(lambda type: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)

    async def test_lambda_unbound_annotation_raises_at_creation(self):
        src = "(lambda :: NoSuchType [x] x)"
        expr, mod = self.compile_expr(src)
        with self.assertRaises(NameError):
            await expr()

    async def test_untyped_lambda_still_works(self):
        src = "(lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertNotIn('return', fn.__annotations__)


# The end.
