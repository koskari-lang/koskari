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
koskari.compile.specials.lambda_

Special form handler for compile/lambda.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...lib import FormalsSpec, FunctionSpec, OpCode
from ...types import is_nil
from ...matcher import BODY, CALLEDBY, FORMALS, LITERAL_KW, MATCH, SYMBOL, VALUE
from ...bootstrap import builtin
from ...syntax import special
from ..compiler import ByteCodeFragment, CompilerMode
from ..helpers import discover_docstring, gather_formals, helper_begin


def helper_function(compiler, caps, selfref):
    """
    Helper for compile/function and compile/lambda.

    :param compiler: Compiler instance
    :param caps: Matcher captures
    :param selfref: Whether to add a self-reference to the function
    :return: ByteCodeFragment
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
    pos = [str(k) for k in pos]
    star = str(star) if star else None
    starstar = str(starstar) if starstar else None

    # annotation expressions evaluate in the defining scope, before the
    # keyword defaults. A str literal is the forward-reference form and
    # stays a constant; anything else compiles as an expression.
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
        if not is_nil(expr):
            defnames.append(str(k))
            frag.merge(compiler.full_compile(expr))

    lamb = compiler.fragment(fragment_name)
    lamb.declare_vars(pos)
    lamb.declare_vars(pre_kwd)
    if star:
        lamb.declare_var(star)
    lamb.declare_vars([str(k) for k, _ in kwonly])
    if starstar:
        lamb.declare_var(starstar)

    # selfref indicates whether the function should receive a direct
    # self-reference at function creation. lambdas with a name get
    # a self-ref, but functions do not. This allows a function to
    # refer to a wrapped definition of itself, such as via decorators,
    # while the lambda form is always purely the exact function.
    if selfref and proper_name is not None:
        lamb.declare_var(proper_name)

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


_FUNCTION_PATTERNS = (
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        FORMALS("formals", scope="body"),
        BODY("body")),
)


@builtin("compile/function")
@special("compile/function", patterns=_FUNCTION_PATTERNS, unpack="match")
def special_function(compiler, caps) -> ByteCodeFragment:
    """
    Compile (function [args] body) or (function name [args] body).

    Unlike lambda, function does not implicitly bind the function
    object into its own scope. This allows a function to refer to an
    outer definition of itself, which may be wrapped such as via
    decorators.
    """

    return helper_function(compiler, caps, selfref=False)


_LAMBDA_PATTERNS = (
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY(),
        FORMALS("formals", scope="body"),
        BODY("body")),
)


@builtin("compile/lambda")
@special("compile/lambda", patterns=_LAMBDA_PATTERNS, unpack="match")
def special_lambda(compiler, caps) -> ByteCodeFragment:
    """
    Compile (lambda [args] body) or (lambda name [args] body).

    The Named form binds name in the function scope for self-reference.
    """

    return helper_function(compiler, caps, selfref=True)


# The end.
