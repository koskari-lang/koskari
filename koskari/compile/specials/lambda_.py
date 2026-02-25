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
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import FormalsSpec, FunctionSpec, nil, OpCode, pair, symbol
from ...types import is_pair, is_symbol
from ..compiler import ByteCodeFragment, Compiler, CompilerMode
from ...exceptions import SpecialError
from ..helpers import gather_formals, helper_begin, special


@special(symbol("compile/lambda"))
def special_lambda(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (lambda [args] body) or (lambda name [args] body).
    Named form binds name in the function scope for self-reference.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("lambda requires formals and body", source)

    first, rest_tail = rest
    if is_symbol(first):
        named = first
        if rest_tail is nil or not is_pair(rest_tail):
            raise SpecialError(
                "named lambda requires formals and body",
                source)
        formals_src, body_src = rest_tail
    else:
        named = None
        formals_src, body_src = first, rest_tail

    if body_src is nil or not is_pair(body_src):
        body_src = pair(body_src, nil)

    proper_name = str(named) if named is not None else None
    fragment_name = proper_name if proper_name is not None else "<lambda>"

    pos, defaults, kwonly, star, starstar = gather_formals(
        formals_src, source=formals_src)
    kw = []
    pos = [str(k) for k in pos]
    star = str(star) if star else None
    starstar = str(starstar) if starstar else None

    frag = compiler.fragment(called_by)
    for k, expr in defaults:
        kw.append(str(k))
        frag.merge(compiler.full_compile(expr))

    if defaults:
        frag.instruction(OpCode.KOS_OP_CONST, tuple(kw))

    lamb = compiler.fragment(fragment_name)
    lamb.declare_vars(pos)
    lamb.declare_vars(kw)
    lamb.declare_vars([str(k) for k, _ in kwonly])
    if star:
        lamb.declare_var(star)
    if starstar:
        lamb.declare_var(starstar)
    if proper_name is not None:
        lamb.declare_var(proper_name)

    lamb = helper_begin(lamb, body_src, True)
    code = compiler.complete(lamb, CompilerMode.FUNCTION)
    fspec = FunctionSpec(proper_name, FormalsSpec(
        tuple(pos), (), tuple(kw), tuple(str(k) for k, _ in kwonly),
        star, starstar), code)
    frag.instruction(OpCode.KOS_OP_CONST, fspec)
    frag.instruction(OpCode.KOS_OP_BUILD_FUNCTION, len(kw))

    return frag


# The end.
