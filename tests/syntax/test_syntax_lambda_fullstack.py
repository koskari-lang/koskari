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
Full-stack tests: compile/decorated-lambda implemented as a @special decorator
(syntax) over matcher patterns with unpack="match". Same compiler
surface as koskari.compile.specials.lambda_.
"""


import pytest

from koskari.compile.compiler import CompilerMode
from koskari.compile.helpers import (
    discover_docstring, gather_formals, helper_begin)
from koskari.compile.specials.lambda_ import _LAMBDA_PATTERNS
from koskari.exceptions import SyntaxMatchError
from koskari.lib import FormalsSpec, FunctionSpec, OpCode, nil, symbol
from koskari.syntax import special
from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


@special(
    "compile/decorated-lambda",
    patterns=_LAMBDA_PATTERNS,
    unpack="match",
)
def special_decorated_lambda(
        compiler,
        caps):
    """
    Decorated lambda: filtered captures per arm; body compiled through
    helper_begin like production lambda
    """

    called_by = caps["called-by"].datum

    named = caps.get("name")
    if named is not None:
        named = named.datum

    return_type_mr = caps.get("return_type")
    return_type_src = return_type_mr.datum if return_type_mr else None

    pos, defaults, kwonly, star, starstar, ann, by_refs = gather_formals(
        caps["formals"])

    body_src = caps["body"].datum

    proper_name = str(named) if named else None
    fragment_name = proper_name or "<lambda>"

    pre_kwd = []
    pre_kwd_syms = [symbol(str(k)) for k, _ in defaults]
    kwonly_syms = [symbol(str(k)) for k, _ in kwonly]
    pos_syms = list(pos)
    pos = [str(k) for k in pos]
    star_sym = star or None
    star = str(star) if star else None
    starstar_sym = starstar or None
    starstar = str(starstar) if starstar else None

    ann_exprs = [(name, bundle['type'])
                 for name, bundle in ann.items() if 'type' in bundle]
    if return_type_src is not None:
        ann_exprs.append(('return', return_type_src))

    frag = compiler.fragment(called_by)

    ann_names = []
    for name, expr in ann_exprs:
        ann_names.append(name)
        if isinstance(expr, str):
            frag.instruction(OpCode.CONST, expr)
        else:
            frag.merge(compiler.full_compile(expr))

    for k, expr in defaults:
        pre_kwd.append(str(k))
        frag.merge(compiler.full_compile(expr))

    defnames = list(pre_kwd)
    for k, expr in kwonly:
        if expr is not nil:
            defnames.append(str(k))
            frag.merge(compiler.full_compile(expr))

    lamb = compiler.fragment(fragment_name)
    lamb.declare_vars(pos_syms)
    lamb.declare_vars(pre_kwd_syms)
    if star_sym:
        lamb.declare_var(star_sym)
    lamb.declare_vars(kwonly_syms)
    if starstar_sym:
        lamb.declare_var(starstar_sym)
    if named is not None:
        lamb.declare_var(named)

    lamb.doc = discover_docstring(body_src)

    with compiler.resolution_scope():
        lamb = helper_begin(lamb, body_src)
    code = compiler.complete(lamb, CompilerMode.FUNCTION)
    fspec = FunctionSpec(proper_name, FormalsSpec(
        tuple(pos), (), tuple(pre_kwd), tuple(str(k) for k, _ in kwonly),
        star, starstar, by_refs), code)
    frag.instruction(OpCode.CONST, fspec)
    frag.instruction(
        OpCode.BUILD_FUNCTION,
        (tuple(defnames), tuple(ann_names), 0, 0))

    return frag


class DecoratedLambdaFullStackBase(SpecialsTestCase):

    def get_glbls(self, **defns):
        out = dict(defns)
        out.setdefault("compile/decorated-lambda", special_decorated_lambda)
        return super().get_glbls(**out)


class TestLambdaNgAnonymous(DecoratedLambdaFullStackBase):

    async def test_anonymous_lambda_identity(self):
        src = "(decorated-lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        result = await fn(42)
        self.assertEqual(result, 42)


    async def test_anonymous_lambda_repr_has_debug_label(self):
        src = "(decorated-lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        repr_str = repr(fn)
        self.assertIn("<lambda>", repr_str)


class TestLambdaNgNamed(DecoratedLambdaFullStackBase):


    async def test_named_lambda_repr_shows_name(self):
        src = "(decorated-lambda fact [n] n)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        repr_str = repr(fn)
        self.assertIn("fact", repr_str)


    async def test_named_lambda_identity(self):
        src = "(decorated-lambda fact [n] n)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        result = await fn(10)
        self.assertEqual(result, 10)


    async def test_lambda_docstring_from_leading_str(self):
        src = '(decorated-lambda [x] "module doc line" x)'
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(fn.__doc__, 'module doc line')
        self.assertEqual(fn.code.extras.get('doc'), 'module doc line')


    async def test_lambda_param_annotation_real_object(self):
        src = "(decorated-lambda [(a :: int)] a)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertEqual(fn.__annotations__, {"a": int})


class TestLambdaNgReturnType(DecoratedLambdaFullStackBase):


    async def test_named_lambda_double_colon_return(self):
        src = "(decorated-lambda f :: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)
        self.assertEqual(await fn(7), 7)


    async def test_named_lambda_type_kw_return(self):
        src = "(decorated-lambda f type: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)


    async def test_anonymous_lambda_double_colon_return(self):
        src = "(decorated-lambda :: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)
        self.assertEqual(await fn(3), 3)


    async def test_anonymous_lambda_type_kw_return(self):
        src = "(decorated-lambda type: int [x] x)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertIs(fn.__annotations__['return'], int)


    async def test_untyped_lambda_still_works(self):
        src = "(decorated-lambda [x] x)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertNotIn('return', fn.__annotations__)


class TestLambdaNgFormalsFeatures(DecoratedLambdaFullStackBase):


    async def test_keyword_default_positional(self):
        src = "(decorated-lambda [x y: 10] y)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(1), 10)
        self.assertEqual(await fn(2, y=3), 3)


    async def test_varargs_star(self):
        src = "(decorated-lambda [a *: rest] rest)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        tail = await fn(1, 2, 3)
        self.assertEqual(tail, (2, 3))


    async def test_kwonly_after_star(self):
        src = "(decorated-lambda [*: _ k: 5] k)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(), 5)


    async def test_starstar_collects(self):
        src = "(decorated-lambda [**: kw] kw)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        bag = await fn(x=1, y=2)
        self.assertIsInstance(bag, dict)


class TestLambdaNgErrors(DecoratedLambdaFullStackBase):


    def test_no_match_raises_special_error(self):
        src = "(decorated-lambda)"
        with self.assertRaises(SyntaxMatchError):
            self.compile_expr(src)


# The end.
