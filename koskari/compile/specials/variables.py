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

Special form handlers for compile/setq, compile/setq-values,
compile/define-values, compile/delq, compile/define,
compile/globals, and compile/locals.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import cons, keyword, nil, OpCode, symbol
from ...matcher import (
    DECL,
    DEFINE,
    LITERAL_KW,
    MATCH,
    PAIR,
    REST,
    SYMBOL,
    VALUE,
    match,
)
from ...types import get_position, is_pair
from ...exceptions import CompilerError, SpecialError
from ..helpers import (
    FORMALS,
    special,
    special_forms,
    unpack_lhs_bind_symbols,
    using_unpack_helper,
)


def _define_name_type_pos_from_match(matched, rest):
    """
    Resolve name symbol, optional type expression, and position from a
    define match (inline DEFINE with ``::`` / ``type:`` or DECL).
    """

    if "decl" in matched:
        decl = matched["decl"]
        namesym = decl.name
        type_expr = decl.type
        pos = decl.source_pos if decl.source_pos is not None else get_position(rest)
    else:
        name_mr = matched["name"]
        namesym = name_mr.source
        rt = matched.get("return_type")
        type_expr = rt.source if rt is not None else None
        pos = name_mr.source_pos if name_mr.source_pos is not None else get_position(rest)
    return namesym, type_expr, pos


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


_VALUES_BIND_PATTERNS = [
    MATCH(SYMBOL("sym"), VALUE("expr")),
    MATCH(PAIR("lhs"), VALUE("expr")),
]


@special_forms(_VALUES_BIND_PATTERNS)
@special(symbol("compile/setq-values"))
def special_setq_values(compiler, source, tc=False):
    """
    Compile (compile/setq-values PATTERN VALUE). Evaluate VALUE, then store
    into existing bindings given by PATTERN (symbol or unpack list, same as
    compile/for-each unpack binding)
    """

    called_by, rest = source
    matched = match(rest, _VALUES_BIND_PATTERNS)
    if matched is None:
        raise SpecialError(
            "setq-values takes a pattern and a value expression", source)

    frag = compiler.fragment(called_by)
    frag.position_of(rest)

    if "sym" in matched:
        lhs = matched["sym"].source
        pattern_mr = matched["sym"]
    else:
        lhs = matched["lhs"].source
        pattern_mr = matched["lhs"]

    expr = matched["expr"].source

    pos = (
        pattern_mr.source_pos
        if pattern_mr.source_pos is not None
        else get_position(rest))

    for namesym in unpack_lhs_bind_symbols(lhs, source=rest):
        frag.request_for_write(str(namesym), position=pos)

    if not is_pair(expr):
        frag.position_of(rest)

    frag.merge(compiler.full_compile(expr))
    frag.merge(
        using_unpack_helper(compiler, lhs, source=rest, declare=False))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special_forms(_VALUES_BIND_PATTERNS)
@special(symbol("compile/define-values"))
def special_define_values(compiler, source, tc=False):
    """
    Compile (compile/define-values PATTERN VALUE). Evaluate VALUE, then bind
    fresh names from PATTERN (symbol or unpack list, same shape as
    compile/setq-values)
    """

    called_by, rest = source
    matched = match(rest, _VALUES_BIND_PATTERNS)
    if matched is None:
        raise SpecialError(
            "define-values takes a pattern and a value expression", source)

    frag = compiler.fragment(called_by)
    frag.position_of(rest)

    if "sym" in matched:
        lhs = matched["sym"].source
    else:
        lhs = matched["lhs"].source

    expr = matched["expr"].source

    if not is_pair(expr):
        frag.position_of(rest)

    frag.merge(compiler.full_compile(expr))
    frag.merge(
        using_unpack_helper(compiler, lhs, source=rest, declare=True))
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


_DEFINE_INLINE_PATTERNS = [
    MATCH(
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        REST("body")),
    MATCH(
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        REST("body")),
    MATCH(
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        VALUE("value")),
    MATCH(
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        VALUE("value")),
    MATCH(
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type")),
    MATCH(
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type")),
]

_DEFINE_DECL_PATTERNS = [
    MATCH(DECL("decl"), FORMALS("formals", scope="body"), REST("body")),
    MATCH(DECL("decl"), VALUE("value")),
    MATCH(DECL("decl")),
]

_DEFINE_PATTERNS = _DEFINE_INLINE_PATTERNS + _DEFINE_DECL_PATTERNS


@special_forms(_DEFINE_PATTERNS)
@special(symbol("compile/define"))
def special_define(compiler, source, tc=False):
    """
    Compile (define X), (define X EXPR), (define X [Y Z] BODY...), or
    typed variants with ``X :: T`` / ``X type: T`` before formals, value,
    or alone (declare-only). Function shorthand is equivalent to
    (define X (lambda X [Y Z] BODY...)).
    """

    called_by, rest = source
    matched = match(rest, _DEFINE_PATTERNS)
    if matched is None:
        raise SpecialError("define requires a name", source)

    namesym, type_expr, pos = _define_name_type_pos_from_match(matched, rest)
    name = str(namesym)

    frag = compiler.fragment(called_by)
    frag.declare_var(name, position=pos)

    if "body" in matched:
        formals = matched["formals"]
        body = matched["body"]
        if type_expr is not None:
            val_expr = cons(
                symbol("compile/lambda"),
                namesym,
                keyword("::"),
                type_expr,
                formals.source,
                body.source)
        else:
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


@special(symbol("compile/globals"))
def special_globals(compiler, source, tc=False):
    """
    Compile (compile/globals). Push the outermost frame namespace (globals)
    """

    called_by, body = source
    if body is not nil:
        raise SpecialError("%s takes no arguments" % called_by, source)

    frag = compiler.fragment(called_by)
    frag.instruction(OpCode.KOS_OP_GLOBALS, 0)
    return frag


@special(symbol("compile/locals"))
def special_locals(compiler, source, tc=False):
    """
    Compile (compile/locals). Push the current frame namespace (locals)
    """

    called_by, body = source
    if body is not nil:
        raise SpecialError("%s takes no arguments" % called_by, source)

    frag = compiler.fragment(called_by)
    frag.instruction(OpCode.KOS_OP_LOCALS, 0)
    return frag


@special_forms(_DEFINE_PATTERNS)
@special(symbol("compile/define-global"))
def special_define_global(compiler, source, tc=False):
    """
    Compile (define-global X), (define-global X EXPR),
    (define-global X [Y Z] BODY...), or typed ``X :: T`` / ``X type: T``
    variants. Explicit module binding for read and write.
    """

    called_by, rest = source
    matched = match(rest, _DEFINE_PATTERNS)
    if matched is None:
        raise SpecialError("define-global requires a name", source)

    namesym, type_expr, pos = _define_name_type_pos_from_match(matched, rest)
    name = str(namesym)

    frag = compiler.fragment(called_by)
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
        if type_expr is not None:
            val_expr = cons(
                symbol("compile/lambda"),
                namesym,
                keyword("::"),
                type_expr,
                formals.source,
                body.source)
        else:
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


# The end.
