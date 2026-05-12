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
:ai-assistant: Claude via Cursor
"""


from ...lib import FormalsSpec, FunctionSpec, OpCode, nil
from ...matcher import FORMALS, LITERAL_KW, MATCH, BODY, SYMBOL, VALUE
from ...stdlib import builtin
from ...syntax import special
from ..compiler import ByteCodeFragment, CompilerMode
from ..helpers import discover_docstring, gather_formals, helper_begin


_LAMBDA_PATTERNS = (
    MATCH(
        SYMBOL("called-by"),
        SYMBOL("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        SYMBOL("called-by"),
        SYMBOL("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        SYMBOL("called-by"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        SYMBOL("called-by"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        SYMBOL("called-by"),
        SYMBOL("name"),
        FORMALS("formals", scope="body"),
        BODY("body")),
    MATCH(
        SYMBOL("called-by"),
        FORMALS("formals", scope="body"),
        BODY("body")),
)


@builtin("compile/lambda")
@special("compile/lambda", patterns=_LAMBDA_PATTERNS, unpack="match")
def special_lambda(compiler, caps, tc=False) -> ByteCodeFragment:
    """
    Compile (lambda [args] body) or (lambda name [args] body).
    Named form binds name in the function scope for self-reference.
    """

    called_by = caps["called-by"].datum

    named = caps.get("name")
    if named is not None:
        named = named.datum

    return_type_mr = caps.get("return_type")
    return_type_src = return_type_mr.datum if return_type_mr else None

    pos, defaults, kwonly, star, starstar, ann = gather_formals(
        caps["formals"])

    body_src = caps["body"].datum

    proper_name = str(named) if named else None
    fragment_name = proper_name or "<lambda>"

    pre_kwd = []
    pos = [str(k) for k in pos]
    star = str(star) if star else None
    starstar = str(starstar) if starstar else None

    frag = compiler.fragment(called_by)
    for k, expr in defaults:
        pre_kwd.append(str(k))
        frag.merge(compiler.full_compile(expr))

    defnames = list(pre_kwd)
    for k, expr in kwonly:
        if expr is not nil:
            defnames.append(str(k))
            frag.merge(compiler.full_compile(expr))

    if defnames:
        frag.instruction(OpCode.KOS_OP_CONST, tuple(defnames))

    lamb = compiler.fragment(fragment_name)
    lamb.declare_vars(pos)
    lamb.declare_vars(pre_kwd)
    if star:
        lamb.declare_var(star)
    lamb.declare_vars([str(k) for k, _ in kwonly])
    if starstar:
        lamb.declare_var(starstar)
    if proper_name is not None:
        lamb.declare_var(proper_name)

    lamb.doc = discover_docstring(body_src)

    with compiler.pragma_scope():
        lamb = helper_begin(lamb, body_src, True)
    code = compiler.complete(lamb, CompilerMode.FUNCTION)
    if return_type_src is not None:
        code.extras['return'] = return_type_src
    fspec = FunctionSpec(proper_name, FormalsSpec(
        tuple(pos), (), tuple(pre_kwd), tuple(str(k) for k, _ in kwonly),
        star, starstar, ann), code)
    frag.instruction(OpCode.KOS_OP_CONST, fspec)
    frag.instruction(OpCode.KOS_OP_BUILD_FUNCTION, len(defnames))

    return frag


# The end.
