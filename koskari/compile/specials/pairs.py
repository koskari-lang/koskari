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


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode, symbol
from ...matcher import BODY, CALLEDBY, MATCH, REST, VALUE
from ...syntax import special
from ...types import is_const, is_nil, is_pair, strip_mark
from ..helpers import requote, splat_call_form


_ONE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("expr")),
]

_CONS_PATTERNS = [
    MATCH(CALLEDBY("called_by"), BODY("exprs")),
]

_BRACKET_PROPER_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("exprs")),
]


def emit_pair_part(compiler, called_by, expr, opcode):
    """
    A fragment for car and cdr: the expression compiled, then opcode
    over the pair it produced

    :return: the fragment
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(expr.position)
    frag.merge(compiler.full_compile(expr.datum))
    frag.instruction(opcode, 1)

    return frag


@builtin("compile/quote")
@special("compile/quote", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_quote(compiler, called_by, expr):
    """
    Compile (compile/quote FORM). A pair is rewritten as its requoted
    construction; any other datum becomes a constant, a symbol as its
    base since quoted data carries no marks
    """

    body = expr.datum

    if is_pair(body) and not is_nil(body):
        body = requote(body)
        body.fill_position(called_by.position)
        return body

    body = strip_mark(body)
    if not is_const(body):
        raise SpecialError(
            f"cannot quote a {type(body).__name__} value", body,
            position=called_by.position)

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    frag.instruction(OpCode.CONST, body)
    return frag


_sym_quote_syntax = symbol("quote-syntax")


@builtin("compile/quote-syntax")
@special("compile/quote-syntax", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_quote_syntax(compiler, called_by, expr):
    """
    Compile (compile/quote-syntax FORM) as ``quote`` does, keeping the
    marks on the symbols. A syntax transformer's patterns and templates
    are quoted this way: a template a macro-generating macro introduced
    carries the outer expansion's marks, and its references must resolve
    to what that expansion bound
    """

    body = expr.datum

    if is_pair(body) and not is_nil(body):
        body = requote(body, quote_sym=_sym_quote_syntax)
        body.fill_position(called_by.position)
        return body

    if not is_const(body):
        raise SpecialError(
            f"cannot quote a {type(body).__name__} value", body,
            position=called_by.position)

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    frag.instruction(OpCode.CONST, body)
    return frag


@builtin("compile/car")
@special("compile/car", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_car(compiler, called_by, expr):
    """
    Compile (compile/car EXPR). Emit code that evaluates EXPR, then pushes
    the car of that pair. One argument. Emits CAR 1; consecutive CAR/CDR
    instructions may be merged by the compiler compress pass.
    """

    return emit_pair_part(compiler, called_by, expr, OpCode.CAR)


@builtin("compile/cdr")
@special("compile/cdr", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_cdr(compiler, called_by, expr):
    """
    Compile (compile/cdr EXPR). Emit code that evaluates EXPR, then pushes
    the cdr of that pair. One argument. Emits CDR 1; consecutive CDR/CAR
    instructions may be merged by the compiler compress pass.
    """

    return emit_pair_part(compiler, called_by, expr, OpCode.CDR)


@builtin("compile/cons")
@special("compile/cons", patterns=_CONS_PATTERNS, unpack="kwargs")
def special_cons(compiler, called_by, exprs):
    """
    Variadic: (compile/cons EXPR1 EXPR2 ... EXPR_TAIL). Compile each
    expression in order (stack bottom = first element, top = tail), then
    emit CONS with arg = number of expressions. At least one argument
    (the tail).
    """

    body = exprs.datum
    forms = list(body.unpack())

    # a keyword among the arguments makes this the cons function
    # applied to complex arguments instead
    rewritten = splat_call_form(called_by.datum, body)
    if rewritten is not None:
        rewritten.cdr.set_position(called_by.position)
        return rewritten

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)

    for form in forms:
        frag.merge(compiler.full_compile(form))

    frag.instruction(OpCode.CONS, len(forms))

    return frag


@builtin("compile/bracket-cons")
@special("compile/bracket-cons", patterns=_CONS_PATTERNS, unpack="kwargs")
def special_bracket_cons(compiler, called_by, exprs):
    """
    Variadic: (compile/bracket-cons EXPR1 EXPR2 ... EXPR_TAIL), the
    bracket twin of compile/cons. Compile each expression in order,
    then emit BRACKET_CONS with arg = number of expressions. At least
    one argument (the tail).
    """

    body = exprs.datum
    forms = list(body.unpack())

    # a keyword among the arguments makes this the bracket-cons
    # function applied to complex arguments instead
    rewritten = splat_call_form(called_by.datum, body)
    if rewritten is not None:
        rewritten.cdr.set_position(called_by.position)
        return rewritten

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)

    for form in forms:
        frag.merge(compiler.full_compile(form))

    frag.instruction(OpCode.BRACKET_CONS, len(forms))

    return frag


@builtin("compile/bracket-proper")
@special("compile/bracket-proper", patterns=_BRACKET_PROPER_PATTERNS,
         unpack="kwargs")
def special_bracket_proper(compiler, called_by, exprs):
    """
    Variadic: (compile/bracket-proper EXPR ...). Compile each expression
    in order, then emit BRACKET_PROPER with arg = number of expressions:
    the proper bracket list of their values, bracket-nil as its tail.
    No arguments is bracket-nil.
    """

    body = exprs.datum
    forms = list(body.unpack())

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)

    for form in forms:
        frag.merge(compiler.full_compile(form))

    frag.instruction(OpCode.BRACKET_PROPER, len(forms))

    return frag


# The end.
