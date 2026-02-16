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
koskari.compile.specials

Special form handlers for the Koskari compiler.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ..lib import car, cdr, FormalsSpec, FunctionSpec, keyword, nil, OpCode, pair, symbol
from ..types import is_pair, is_symbol
from .compiler import ByteCodeFragment, Compiler, CompilerMode
from .exceptions import CompilerException
from .helpers import gather_formals, gather_parameters, helper_begin


SPECIALS = {}


def special(form_symbol):
    """
    Decorator that registers a function as a special form handler.
    The decorated function gets a compile_impl attribute for dispatch.
    """

    def decorator(fn):
        SPECIALS[form_symbol] = fn
        fn.compile_impl = fn
        return fn

    return decorator


@special(symbol("compile/bytecode"))
def special_bytecode(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    fragment = compiler.fragment(called_by)

    for segm in body.unpack():
        op, opa = segm
        if opa is nil:
            opa = 0
        else:
            opa, rest = opa

        if isinstance(op, int):
            opc = OpCode(op)

        elif isinstance(op, (str, symbol, keyword)):
            op = str(op)
            if not op.startswith("KOS_OP_"):
                if op.startswith("OP_"):
                    op = "KOS_" + op
                else:
                    op = "KOS_OP_" + op

            opc = getattr(OpCode, op, None)

        else:
            opc = None

        if opc is None:
            raise TypeError("invalid Koskari OpCode: %r" % op)

        fragment.position_of(segm)
        fragment.instruction(opc, opa)

    return fragment


@special(symbol("compile/begin"))
def special_begin(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source
    fragment = compiler.fragment(called_by)
    return helper_begin(fragment, body, tc)



@special(symbol("compile/call"))
def special_call(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    fragment = compiler.fragment(called_by)

    # maybe check for (get-attr) here and transform to call-method
    fragment.position_of(body)
    obj, rest = body
    fragment.merge(compiler.full_compile(obj))

    params = gather_parameters(rest)
    pos, kwds, values, vargs, vkwds = params

    if vargs or vkwds:
        # we'll be using CALL_STAR
        opa = 0
        if pos or vargs:
            opa = opa | 1
        if values or vkwds:
            opa = opa | 2

        if pos:
            for expr in pos:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.OP_BUILD_ARGS, len(pos))

        if vargs:
            fragment.merge(compiler.full_compile(vargs, False))
            if pos:
                fragment.instruction(OpCode.OP_COMBINE_ARGS, 2)

        if values:
            for expr in values:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.OP_BUILD_KWDS_NAMES,
                                 tuple(str(k) for k in kwds))

        if vkwds:
            fragment.merge(compiler.full_compile(vkwds, False))
            if values:
                fragment.instruction(OpCode.OP_COMBINE_KWDS, 2)

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.OP_TAIL_CALL_STAR, opa)
        else:
            fragment.instruction(OpCode.OP_CALL_STAR, opa)

    elif kwds:
        # we'll use CALL_NAMES
        opa = len(pos)

        for expr in pos:
            fragment.merge(compiler.full_compile(expr, False))

        for expr in values:
            fragment.merge(compiler.full_compile(expr, False))
        fragment.instruction(OpCode.OP_CONST,
                             tuple(str(k) for k in kwds))

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.OP_TAIL_CALL_NAMES, opa)
        else:
            fragment.instruction(OpCode.OP_CALL_NAMES, opa)

    else:
        # easy mode, we can just use CALL
        for expr in pos:
            fragment.merge(compiler.full_compile(expr, False))

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.OP_TAIL_CALL, len(pos))
        else:
            fragment.instruction(OpCode.OP_CALL, len(pos))

    return fragment


@special(symbol("lambda"))
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
        raise CompilerException("lambda requires formals and body", source)

    first = car(rest)
    if is_symbol(first):
        named = first
        rest = cdr(rest)
        if rest is nil or not is_pair(rest):
            raise CompilerException("named lambda requires formals and body",
                                   source)
        formals_src = car(rest)
        body_src = cdr(rest)
    else:
        named = None
        formals_src = first
        body_src = cdr(rest)

    if body_src is nil or not is_pair(body_src):
        body_src = pair(body_src, nil)

    proper_name = str(named) if named is not None else None
    fragment_name = proper_name if proper_name is not None else "<lambda>"

    pos, defaults, kwonly, star, starstar = gather_formals(formals_src)
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
