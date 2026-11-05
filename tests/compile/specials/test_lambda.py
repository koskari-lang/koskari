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


    async def test_marked_name_gives_a_plain_code_name(self):
        """
        A lambda a template names carries the expansion mark on its
        binding symbol only: the code is named by the plain name
        """
        src = """
        (var mk
          (syntax-rules mk []
            [(mk) (lambda each [] 1)]))
        """
        expr, mod = self.compile_expr(src)
        await expr()
        defn = {'compile/mk': vars(mod)['mk']}
        expr, mod = self.compile_expr("(mk)", **defn)
        fn = await expr()
        self.assertEqual("each", fn.code.name)
        self.assertEqual("each", fn.__name__)


    async def test_named_lambda_identity(self):
        src = "(lambda fact [n] n)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        result = await fn(10)
        self.assertEqual(result, 10)


    async def test_named_lambda_recurses_through_its_name_repeatedly(self):
        """
        A named lambda recursing through its own name answers the same
        on every call: the later calls run on frames the earlier ones
        left in the function's cache, each with the name rebound
        """

        src = """
            (lambda fact [n]
              (cond [(< n 2) 1]
                    [else: (* n (fact (- n 1)))]))
        """
        expr, mod = self.compile_expr(src)
        fn = await expr()
        for _ in range(3):
            self.assertEqual(await fn(10), 3628800)


    async def test_positional_formal_sharing_the_name_wins(self):
        """
        A positional formal named like the lambda shadows the self
        name: the body sees the argument
        """

        expr, mod = self.compile_expr("(lambda f [f] f)")
        fn = await expr()
        self.assertEqual(await fn(1), 1)


    async def test_keyword_only_formal_sharing_the_name_wins(self):
        """
        A keyword-only formal named like the lambda shadows the self
        name: the body sees the argument
        """

        expr, mod = self.compile_expr("(lambda f [*: nil f] f)")
        fn = await expr()
        self.assertEqual(await fn(f=2), 2)


    async def test_named_lambda_records_its_self_slot(self):
        """
        A named lambda whose body names itself holds itself in a local,
        and its code records that local's slot
        """

        src = """
            (lambda fact [n]
              (cond [(< n 2) 1]
                    [else: (* n (fact (- n 1)))]))
        """
        expr, mod = self.compile_expr(src)
        fn = await expr()
        local_syms = fn.code.namespace_spec.local_syms
        self.assertEqual(fn.code.self_slot, local_syms.index('fact'))
        self.assertEqual(await fn(5), 120)


    async def test_named_lambda_not_naming_itself_has_no_self_local(self):
        """
        A named lambda whose body never names itself has no local for
        the name and records no self slot
        """

        expr, mod = self.compile_expr("(lambda f [x] x)")
        fn = await expr()
        self.assertNotIn('f', fn.code.namespace_spec.local_syms)
        self.assertEqual(fn.code.self_slot, -1)


    async def test_inner_lambda_self_call_binds(self):
        """
        A self-call from a lambda nested in the named lambda's body
        reaches the named lambda through its self local
        """

        src = """
            (lambda count-down [n]
              ((lambda []
                 (cond [(< n 1) n]
                       [else: (count-down (- n 1))]))))
        """
        expr, mod = self.compile_expr(src)
        fn = await expr()
        local_syms = fn.code.namespace_spec.local_syms
        self.assertEqual(
            fn.code.self_slot, local_syms.index('count-down'))
        self.assertEqual(await fn(4), 0)


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


    async def test_lambda_required_after_bracket_default_rejected(self):
        """
        A required positional after a (name default) cell is a compile
        error rather than a silent reordering of the caller's arguments
        """

        src = "(lambda [a (b 10) c] #(a b c))"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src)
        self.assertIn("'c'", str(cm.exception))
        self.assertEqual((1, 18), cm.exception.position)


    async def test_lambda_duplicate_formal_rejected_at_its_position(self):
        """
        A name declared twice in one formals list is a compile error
        located at the second occurrence, whatever kind it is
        """

        cases = (
            ("(lambda [a a] a)", (1, 11)),
            ("(lambda [a (a 1)] a)", (1, 11)),
            ("(lambda [a a: 1] a)", (1, 11)),
            ("(lambda [a *: a] a)", (1, 14)),
            ("(lambda [a **: a] a)", (1, 15)),
            ("(lambda [a *: r r: 2] a)", (1, 16)),
        )
        for src, position in cases:
            with self.assertRaises(SpecialError) as cm:
                self.compile_expr(src)
            self.assertIn("duplicate formal", str(cm.exception), src)
            self.assertEqual(position, cm.exception.position, src)


    async def test_lambda_trailing_bracket_defaults(self):
        src = "(lambda [a (b 10) (c 20)] #(a b c))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual((1, 2, 3), await fn(1, 2, 3))
        self.assertEqual((1, 10, 20), await fn(1))


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


class TestFunctionNeverBindsItself(SpecialsTestCase):
    """
    A function never binds its own name: a reference to the name in
    its body reaches the enclosing binding, and a formal or local of
    that name is an ordinary one
    """

    async def test_name_in_body_is_a_closure(self):
        expr, mod = self.compile_expr(
            '(let [[f "outer"]] (function f [] f))')
        fn = await expr()
        self.assertIn('f', fn.code.namespace_spec.closure_syms)
        self.assertEqual(fn.code.self_slot, -1)
        self.assertEqual(await fn(), "outer")


    async def test_formal_sharing_the_name_is_a_formal(self):
        expr, mod = self.compile_expr("(function f [f] f)")
        fn = await expr()
        self.assertEqual(await fn(4), 4)


    async def test_local_sharing_the_name_starts_unbound(self):
        expr, mod = self.compile_expr(
            "(function f [] (cond [False (define f 1)]) f)")
        fn = await expr()
        with self.assertRaises(NameError):
            await fn()


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
