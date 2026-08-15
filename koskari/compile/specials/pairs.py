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
"""


from ...lib import OpCode, symbol, cons
from ...types import get_position, is_nil, is_pair, is_keyword
from ...exceptions import SpecialError
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...syntax import special
from ..helpers import requote


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/quote")
@special("compile/quote", patterns=_DEFAULT_PATTERNS)
def special_quote(compiler, source, tc=False):
    called_by, body = source

    if is_nil(body) or not is_pair(body):
        raise SpecialError("Too few arguments to quote", source)

    body, rest = body
    if not is_nil(rest):
        raise SpecialError("Too many arguments to quote", source)

    if is_pair(body) and not is_nil(body):
        body = requote(body)
        body.fill_position(get_position(source))
        return body

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.instruction(OpCode.CONST, body)
    return frag


@builtin("compile/car")
@special("compile/car", patterns=_DEFAULT_PATTERNS)
def special_car(compiler, source, tc=False):
    """
    Compile (compile/car EXPR). Emit code that evaluates EXPR, then pushes
    the car of that pair. One argument. Emits CAR 1; consecutive CAR/CDR
    instructions may be merged by the compiler compress pass.
    """

    called_by, body = source
    if is_nil(body) or not is_pair(body):
        raise SpecialError("compile/car requires one argument", source)

    expr, rest = body
    if not is_nil(rest):
        raise SpecialError("compile/car takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.CAR, 1)

    return frag


@builtin("compile/cdr")
@special("compile/cdr", patterns=_DEFAULT_PATTERNS)
def special_cdr(compiler, source, tc=False):
    """
    Compile (compile/cdr EXPR). Emit code that evaluates EXPR, then pushes
    the cdr of that pair. One argument. Emits CDR 1; consecutive CDR/CAR
    instructions may be merged by the compiler compress pass.
    """

    called_by, body = source
    if is_nil(body) or not is_pair(body):
        raise SpecialError("compile/cdr requires one argument", source)

    expr, rest = body
    if not is_nil(rest):
        raise SpecialError("compile/cdr takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.CDR, 1)

    return frag


@builtin("compile/cons")
@special("compile/cons", patterns=_DEFAULT_PATTERNS)
def special_cons(compiler, source, tc=False):
    """
    Variadic: (compile/cons EXPR1 EXPR2 ... EXPR_TAIL). Compile each
    expression in order (stack bottom = first element, top = tail), then
    emit CONS with arg = number of expressions. At least one argument
    (the tail).
    """

    called_by, body = source
    if is_nil(body) or not is_pair(body):
        raise SpecialError(
            "compile/cons requires at least one argument",
            source)

    exprs = list(body.unpack())

    for fm in exprs:
        if is_keyword(fm):
            return cons(symbol("call"), source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)

    for expr in exprs:
        frag.merge(compiler.full_compile(expr))

    frag.instruction(OpCode.CONS, len(exprs))

    return frag


# The end.
