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
koskari.compile.specials.pairs

Special form handlers for compile/quote, compile/car, compile/cdr, and
compile/cons.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ...types import get_position, is_nil, is_pair, set_position
from ...exceptions import SpecialError
from ..helpers import requote, special


@special(symbol("compile/quote"))
def special_quote(compiler, source, tc=False):
    called_by, body = source

    if body is nil or not is_pair(body):
        raise SpecialError("Too few arguments to quote", source)

    body, rest = body
    if rest is not nil:
        raise SpecialError("Too many arguments to quote", source)

    if is_pair(body) and not is_nil(body):
        body = requote(body)
        set_position(body, get_position(source))
        return body

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.instruction(OpCode.KOS_OP_CONST, body)
    return frag


@special(symbol("compile/car"))
def special_car(compiler, source, tc=False):
    """
    Compile (compile/car EXPR). Emit code that evaluates EXPR, then pushes
    the car of that pair. One argument.
    """

    called_by, body = source
    if body is nil or not is_pair(body):
        raise SpecialError("compile/car requires one argument", source)

    expr, rest = body
    if rest is not nil:
        raise SpecialError("compile/car takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_CAR, 0)

    return frag


@special(symbol("compile/cdr"))
def special_cdr(compiler, source, tc=False):
    """
    Compile (compile/cdr EXPR). Emit code that evaluates EXPR, then pushes
    the cdr of that pair. One argument.
    """

    called_by, body = source
    if body is nil or not is_pair(body):
        raise SpecialError("compile/cdr requires one argument", source)

    expr, rest = body
    if rest is not nil:
        raise SpecialError("compile/cdr takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_CDR, 0)

    return frag


@special(symbol("compile/cons"))
def special_cons(compiler, source, tc=False):
    """
    Variadic: (compile/cons EXPR1 EXPR2 ... EXPR_TAIL). Compile each
    expression in order (stack bottom = first element, top = tail), then
    emit CONS with arg = number of expressions. At least one argument
    (the tail).
    """

    called_by, body = source
    if body is nil or not is_pair(body):
        raise SpecialError(
            "compile/cons requires at least one argument",
            source)

    exprs = list(body.unpack())
    frag = compiler.fragment(called_by)
    frag.position_of(source)

    for expr in exprs:
        frag.merge(compiler.full_compile(expr))

    frag.instruction(OpCode.KOS_OP_CONS, len(exprs))

    return frag


# The end.
