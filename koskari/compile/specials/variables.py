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
koskari.compile.specials.variables

Special form handlers for compile/setq, compile/refq, compile/bindq,
compile/delq, and compile/define.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import cons, nil, OpCode, symbol
from ...matcher import DEFINE, MATCH, REST, VALUE, match
from ...types import is_pair
from ...exceptions import CompilerError, SpecialError
from ..helpers import FORMALS, is_symbolish, special, special_forms
from ..syntax import build_syntax_rules_transformer


@special(symbol("compile/setq"))
def special_setq(compiler, source, tc=False):
    """
    Compile (compile/setq SYM EXPR). Store the value of EXPR into the existing
    binding SYM
    """

    called_by, (sym, body) = source
    expr, rest = body

    sym = str(sym)

    if rest is not nil:
        raise SpecialError("setq takes two arguments", source)

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(body)

    frag.request_var(sym)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/refq"))
def special_refq(compiler, source, tc=False):
    """
    Compile (compile/refq SYM). Load the value of the binding named SYM
    """

    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise SpecialError("refq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_REF, sym)

    return frag


@special(symbol("compile/bindq"))
def special_bindq(compiler, source, tc=False):
    """
    Compile (compile/bindq SYM EXPR). Declare a new binding SYM and initialize
    it to EXPR
    """

    called_by, (sym, body) = source
    expr, rest = body

    sym = str(sym)

    if rest is not nil:
        raise SpecialError("bindq takes two arguments", source)

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(body)

    frag.declare_var(sym)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_BIND, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/delq"))
def special_delq(compiler, source, tc=False):
    """
    Compile (compile/delq SYM). Clear the binding SYM, resetting it to unset
    """

    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise SpecialError("delq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_CLEAR, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_DEFINE_PATTERNS = [
    MATCH(DEFINE("name"), FORMALS("formals", scope="body"), REST("body")),
    MATCH(DEFINE("name"), VALUE("value")),
    MATCH(DEFINE("name")),
]


@special_forms(_DEFINE_PATTERNS)
@special(symbol("compile/define"))
def special_define(compiler, source, tc=False):
    """
    Compile (define X), (define X EXPR), or (define X [Y Z] BODY...).
    Declaration-only; declare-and-store; or function shorthand equivalent
    to (define X (lambda X [Y Z] BODY...)).
    """

    called_by, rest = source
    matched = match(rest, _DEFINE_PATTERNS)
    if matched is None:
        raise SpecialError("define requires a name", source)

    name_val = matched["name"]
    sym = str(name_val.source if hasattr(name_val, 'source') else name_val)
    frag = compiler.fragment(called_by)
    frag.declare_var(sym)

    if "body" in matched:
        formals_val = matched["formals"]
        formals_raw = formals_val.source if hasattr(formals_val, 'source') else formals_val
        body_val = matched["body"]
        body_raw = body_val.source if hasattr(body_val, 'source') else body_val
        name_val = matched["name"]
        name_raw = name_val.source if hasattr(name_val, 'source') else name_val
        val_expr = cons(symbol("compile/lambda"), name_raw,
                        formals_raw, body_raw)

    elif "value" in matched:
        val_val = matched["value"]
        val_expr = val_val.source if hasattr(val_val, 'source') else val_val

    else:
        frag.instruction(OpCode.KOS_OP_CONST, None)
        return frag

    if not is_pair(val_expr):
        frag.position_of(rest)

    frag.merge(compiler.full_compile(val_expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/define-syntax"))
def special_define_syntax(compiler, source, tc=False):
    """
    Compile (define-syntax NAME (syntax-rules ...)). Registers a
    deterministic matcher/transformer object under compile/NAME.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("define-syntax requires a name and transformer", source)

    name, rest_tail = rest
    if not is_symbolish(name):
        raise SpecialError("define-syntax name must be a symbol", source)

    if rest_tail is nil or not is_pair(rest_tail) or rest_tail[1] is not nil:
        raise SpecialError("define-syntax takes exactly two arguments", source)

    transformer_spec = rest_tail[0]

    try:
        transformer = build_syntax_rules_transformer(name, transformer_spec)
    except CompilerError as ex:
        raise SpecialError(str(ex), source) from ex

    tagged = transformer.to_tuple()
    build_call = cons(symbol("build-syntax-rules"), str(name), tagged, nil)

    frag = compiler.fragment(called_by)
    frag.declare_var(str(name))

    frag.merge(compiler.full_compile(build_call))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, str(name))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
