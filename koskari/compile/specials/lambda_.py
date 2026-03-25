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


from ...lib import FormalsSpec, FunctionSpec, OpCode, pair, symbol
from ..compiler import ByteCodeFragment, Compiler, CompilerMode
from ...exceptions import SpecialError
from ...matcher import MATCH, REST, SYMBOL, match
from ..helpers import (
    FORMALS, discover_docstring, helper_begin, special, special_forms)


_LAMBDA_PATTERNS = [
    MATCH(SYMBOL("name"), FORMALS("formals", scope="body"), REST("body")),
    MATCH(FORMALS("formals", scope="body"), REST("body")),
]


@special_forms(_LAMBDA_PATTERNS)
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
    matched = match(rest, _LAMBDA_PATTERNS)
    if matched is None:
        raise SpecialError("lambda requires formals and body", source)

    named = matched.get("name", None)
    if named is not None:
        named = named.source

    formals = matched["formals"]
    pos = formals.positional
    defaults = formals.defaults
    kwonly = formals.kwonly
    star = formals.star
    starstar = formals.starstar

    body_src = matched["body"].source

    proper_name = str(named) if named else None
    fragment_name = proper_name or "<lambda>"

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

    lamb.doc = discover_docstring(body_src)

    with compiler.pragma_scope():
        lamb = helper_begin(lamb, body_src, True)
    code = compiler.complete(lamb, CompilerMode.FUNCTION)
    fspec = FunctionSpec(proper_name, FormalsSpec(
        tuple(pos), (), tuple(kw), tuple(str(k) for k, _ in kwonly),
        star, starstar), code)
    frag.instruction(OpCode.KOS_OP_CONST, fspec)
    frag.instruction(OpCode.KOS_OP_BUILD_FUNCTION, len(kw))

    return frag


# The end.
