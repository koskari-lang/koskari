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

Special form handlers for compile/setq, compile/delq, compile/define,
and compile/define-syntax.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import cons, nil, OpCode, symbol
from ...matcher import DEFINE, MATCH, REST, SYMBOL, VALUE, match
from ...types import get_position, is_pair
from ...exceptions import CompilerError, SpecialError
from ..helpers import FORMALS, special, special_forms
from ..syntax import build_syntax_rules_transformer


_SETQ_PATTERNS = [MATCH(SYMBOL("sym"), VALUE("expr"))]


@special_forms(_SETQ_PATTERNS)
@special(symbol("compile/setq"))
def special_setq(compiler, source, tc=False):
    """
    Compile (compile/setq SYM EXPR). Store the value of EXPR into the existing
    binding SYM
    """

    called_by, rest = source
    matched = match(rest, _SETQ_PATTERNS)
    if matched is None:
        raise SpecialError("setq takes two arguments", source)

    sym = str(matched["sym"].source)
    expr = matched["expr"].source

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(rest)

    pos = matched["sym"].source_pos if hasattr(matched["sym"], "source_pos") else get_position(rest)
    frag.request_for_write(sym, position=pos)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_DELQ_PATTERNS = [MATCH(SYMBOL("sym"))]


@special_forms(_DELQ_PATTERNS)
@special(symbol("compile/delq"))
def special_delq(compiler, source, tc=False):
    """
    Compile (compile/delq SYM). Clear the binding SYM, resetting it to unset
    """

    called_by, rest = source
    matched = match(rest, _DELQ_PATTERNS)
    if matched is None:
        raise SpecialError("delq takes one argument", source)

    sym = str(matched["sym"].source)

    frag = compiler.fragment(called_by)

    frag.position_of(rest)
    pos = matched["sym"].source_pos if hasattr(matched["sym"], "source_pos") else get_position(rest)
    frag.request_for_write(sym, position=pos)
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

    namesym = matched["name"].source
    name = str(namesym)

    frag = compiler.fragment(called_by)
    pos = matched["name"].source_pos if hasattr(matched["name"], "source_pos") else get_position(rest)
    frag.declare_var(name, position=pos)

    if "body" in matched:
        formals = matched["formals"]
        body = matched["body"]
        val_expr = cons(symbol("compile/lambda"), namesym,
                        formals.source, body.source)

    elif "value" in matched:
        val_expr = matched["value"].source

    else:
        frag.instruction(OpCode.KOS_OP_CONST, None)
        return frag

    if not is_pair(val_expr):
        frag.position_of(rest)

    frag.merge(compiler.full_compile(val_expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_DEFINE_GLOBAL_PATTERNS = [
    MATCH(DEFINE("name"), FORMALS("formals", scope="body"), REST("body")),
    MATCH(DEFINE("name"), VALUE("value")),
    MATCH(DEFINE("name")),
]


@special_forms(_DEFINE_GLOBAL_PATTERNS)
@special(symbol("compile/define-global"))
def special_define_global(compiler, source, tc=False):
    """
    Compile (define-global X), (define-global X EXPR), or
    (define-global X [Y Z] BODY...). Explicit module binding for read and write.
    """

    called_by, rest = source
    matched = match(rest, _DEFINE_GLOBAL_PATTERNS)
    if matched is None:
        raise SpecialError("define-global requires a name", source)

    namesym = matched["name"].source
    name = str(namesym)

    frag = compiler.fragment(called_by)
    pos = matched["name"].source_pos if hasattr(matched["name"], "source_pos") else get_position(rest)
    try:
        frag.request_global(name, position=pos)
    except CompilerError:
        raise SpecialError(
            '%r cannot be both local (define) and global (define-global).'
            % (name,),
            source) from None

    if "body" in matched:
        formals = matched["formals"]
        body = matched["body"]
        val_expr = cons(symbol("compile/lambda"), namesym,
                        formals.source, body.source)

    elif "value" in matched:
        val_expr = matched["value"].source

    else:
        frag.instruction(OpCode.KOS_OP_CONST, None)
        return frag

    if not is_pair(val_expr):
        frag.position_of(rest)

    frag.merge(compiler.full_compile(val_expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_DEFINE_SYNTAX_PATTERNS = [MATCH(DEFINE("name"), VALUE("transformer"))]


@special_forms(_DEFINE_SYNTAX_PATTERNS)
@special(symbol("compile/define-syntax"))
def special_define_syntax(compiler, source, tc=False):
    """
    Compile (define-syntax NAME (syntax-rules ...)). Registers a
    deterministic matcher/transformer object under compile/NAME.
    """

    called_by, rest = source
    matched = match(rest, _DEFINE_SYNTAX_PATTERNS)
    if matched is None:
        raise SpecialError("define-syntax requires a name and transformer", source)

    namesym = matched["name"].source
    transformer_spec = matched["transformer"].source

    try:
        transformer = build_syntax_rules_transformer(namesym, transformer_spec)
    except CompilerError as ex:
        raise SpecialError(str(ex), source) from ex

    tagged = transformer.to_tuple()
    build_call = cons(symbol("build-syntax-rules"), str(namesym), tagged, nil)

    frag = compiler.fragment(called_by)
    pos = matched["name"].source_pos
    frag.declare_var(str(namesym), position=pos)

    frag.merge(compiler.full_compile(build_call))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, str(namesym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
