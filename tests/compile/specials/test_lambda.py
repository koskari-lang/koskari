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


import pytest

from koskari.exceptions import SpecialError

from . import SpecialsTestCase


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


    async def test_lambda_keyword_headed_formal_default_and_annotation(self):
        src = "(lambda [[x :: int] [y: 1 :: int]] (add x y))"
        expr, mod = self.compile_expr(src, int=int, add=lambda a, b: a + b)
        fn = await expr()
        self.assertEqual(fn.__annotations__, {"x": int, "y": int})
        self.assertEqual(await fn(1), 2)
        self.assertEqual(await fn(1, 5), 6)
        self.assertEqual(await fn(1, y=7), 8)


    async def test_lambda_keyword_headed_formal_bare_double_colon_rejected(self):
        src = "(lambda [[:: int]] 1)"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src, int=int)
        self.assertIn("[(name :: int)]", str(cm.exception))


    async def test_lambda_bare_double_colon_in_formals_rejected(self):
        src = "(lambda [n :: int] n)"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src, int=int)
        self.assertIn("[(n :: int)]", str(cm.exception))


    async def test_lambda_bare_double_colon_after_star_rejected(self):
        src = "(lambda [n *: rest :: int] n)"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src, int=int)
        self.assertIn("[(n :: int)]", str(cm.exception))


    async def test_lambda_bare_double_colon_leading_rejected(self):
        src = "(lambda [:: int] 1)"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src, int=int)
        self.assertIn("[(name :: int)]", str(cm.exception))


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


class TestLambdaKeywordOnlyDefaults(SpecialsTestCase):
    """
    A keyword-only parameter is required when it is a bare name after
    the star segment, and defaulted when it carries a value, nil
    included
    """

    async def test_nil_is_a_default(self):
        from koskari.lib import nil

        expr, mod = self.compile_expr("(lambda [*: nil k: ()] k)")
        fn = await expr()
        self.assertIs(await fn(), nil)
        self.assertEqual(await fn(k=3), 3)

    async def test_bare_name_is_required(self):
        expr, mod = self.compile_expr("(lambda [*: nil k] k)")
        fn = await expr()
        with self.assertRaises(TypeError):
            await fn()
        self.assertEqual(await fn(k=3), 3)


# The end.
