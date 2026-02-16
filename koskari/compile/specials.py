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


from ..lib import keyword, nil, OpCode, pair, symbol
from .compiler import ByteCodeFragment, Compiler
from .helpers import gather_parameters, helper_begin


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


# The end.
