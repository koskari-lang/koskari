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


from ...exceptions import CompilerError, SpecialError
from ...lib import Datum, OpCode, cons, keyword, nil, symbol
from ...matcher import (BODY, DECL, DEFINE, FORMALS, LITERAL_KW, MATCH, PAIR,
                           REST, SYMBOL, VALUE)
from ...stdlib import builtin
from ...syntax import special
from ...types import is_pair
from ..helpers import (_parse_decl_tail, parse_decl,
                          unpack_lhs_bind_symbols, using_unpack_helper)


def _define_name_type_pos_from_match(matched):
    """
    Resolve name symbol, optional type expression from a
    define match (inline DEFINE with ``::`` / ``type:`` or DECL).

    Prefer the DEFINE arm when ``name`` is present so ``decl`` is never
    mistaken for a declaration result on patterns that only bind ``name``.

    DECL composite captures are native GROUP dicts or :class:`DatumFrame`
    values; compile match may adapt the decl slot like a FORMALS group.
    When the adapted value has no name attribute, recover the symbol and
    optional type with parse_decl on ``decl.source`` (whole-cell spine).
    When ``name`` is set but ``type`` is not (``name`` + ``decl_tail`` DECL
    arm), parse ``decl_tail`` like a declaration attribute list.
    """

    if "name" in matched:
        name_mr = matched["name"]
        namesym = name_mr.datum
        rt = matched.get("return_type")
        type_expr = rt.datum if rt else None

    else:
        decl = matched["decl"]
        name_cell = decl.get("name")
        if name_cell is not None:
            namesym = name_cell.datum
            tcell = decl.get("type")
            type_expr = tcell.datum if tcell else None
            if type_expr is None:
                dtail = decl.get("decl_tail")
                if dtail is not None:
                    ann = _parse_decl_tail(dtail.datum, None)
                    type_expr = ann.get("type")
        else:
            namesym, type_expr = parse_decl(decl.source)

    return namesym, type_expr


_DEFAULT_PATTERNS = (MATCH(SYMBOL("_"), REST("rest")),)


_SETQ_PATTERNS = [
    MATCH(SYMBOL("_"), SYMBOL("sym"), VALUE("expr")),
]


@builtin("compile/setq")
@special("compile/setq", patterns=_SETQ_PATTERNS, unpack="match")
def special_setq(compiler, caps, tc=False):
    """
    Compile (compile/setq SYM EXPR). Store the value of EXPR into the existing
    binding SYM
    """

    sym = str(caps["sym"].datum)
    pos = caps["sym"].position
    expr = caps["expr"].datum

    frag = compiler.fragment(caps["_"].datum)

    if not is_pair(expr):
        frag.position(pos)

    frag.request_for_write(sym, position=pos)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_VALUES_BIND_PATTERNS = [
    MATCH(SYMBOL("_"), SYMBOL("lhs"), VALUE("expr")),
    MATCH(SYMBOL("_"), PAIR("lhs"), VALUE("expr")),
]


@builtin("compile/setq-values")
@special("compile/setq-values", patterns=_VALUES_BIND_PATTERNS, unpack="match")
def special_setq_values(compiler, caps, tc=False):
    """
    Compile (compile/setq-values PATTERN VALUE). Evaluate VALUE, then store
    into existing bindings given by PATTERN (symbol or unpack list, same as
    compile/for-each unpack binding)
    """

    called_by = caps["_"].datum
    pos = caps["_"].position

    lhs_mr = caps["lhs"]
    expr_mr = caps["expr"]

    frag = compiler.fragment(called_by)

    lhs = lhs_mr.datum

    for namesym in unpack_lhs_bind_symbols(lhs, flatten=True):
        frag.request_for_write(str(namesym), position=pos)

    frag.position(expr_mr.position)
    frag.merge(compiler.full_compile(expr_mr.datum))
    frag.merge(
        using_unpack_helper(compiler, lhs, declare=False))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@builtin("compile/define-values")
@special("compile/define-values", patterns=_VALUES_BIND_PATTERNS, unpack="match")
def special_define_values(compiler, caps, tc=False):
    """
    Compile (compile/define-values PATTERN VALUE). Evaluate VALUE, then bind
    fresh names from PATTERN (symbol or unpack list, same shape as
    compile/setq-values)
    """

    called_by = caps["_"].datum
    pos = caps["_"].position

    lhs_mr = caps["lhs"]
    expr_mr = caps["expr"]

    frag = compiler.fragment(called_by)

    lhs = lhs_mr.datum

    frag.position(expr_mr.position)
    frag.merge(compiler.full_compile(expr_mr.datum))
    frag.merge(
        using_unpack_helper(compiler, lhs, declare=True))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_DELQ_PATTERNS = [
    MATCH(SYMBOL("_"), SYMBOL("sym")),
]


@builtin("compile/delq")
@special("compile/delq", patterns=_DELQ_PATTERNS, unpack="match")
def special_delq(compiler, caps, tc=False):
    """
    Compile (compile/delq SYM). Clear the binding SYM, resetting it to unset
    """

    called_by = caps["_"].datum
    pos = caps["_"].position
    sym = caps["sym"].datum

    frag = compiler.fragment(called_by)

    frag.position(pos)
    frag.request_for_write(str(sym), position=pos)
    frag.instruction(OpCode.KOS_OP_VAR_CLEAR, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_DEFINE_INLINE_PATTERNS = [
    MATCH(
        SYMBOL("_"),
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        REST("body")),
    MATCH(
        SYMBOL("_"),
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        FORMALS("formals", scope="body"),
        REST("body")),
    MATCH(
        SYMBOL("_"),
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        VALUE("value")),
    MATCH(
        SYMBOL("_"),
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        VALUE("value")),
    MATCH(
        SYMBOL("_"),
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type")),
    MATCH(
        SYMBOL("_"),
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type")),
]

_DEFINE_DECL_PATTERNS = [
    MATCH(SYMBOL("_"), DECL("decl"), FORMALS("formals", scope="body"), BODY("body")),
    MATCH(SYMBOL("_"), DECL("decl"), VALUE("value")),
    MATCH(SYMBOL("_"), DECL("decl")),
]

_DEFINE_PATTERNS = _DEFINE_INLINE_PATTERNS + _DEFINE_DECL_PATTERNS


@builtin("compile/define")
@special("compile/define", patterns=_DEFINE_PATTERNS, unpack="match")
def special_define(compiler, caps, tc=False):
    """
    Compile (define X), (define X EXPR), (define X [Y Z] BODY...), or
    typed variants with ``X :: T`` / ``X type: T`` before formals, value,
    or alone (declare-only). Function shorthand is equivalent to
    (define X (lambda X [Y Z] BODY...)).
    """

    called_by = caps["_"].datum
    pos = caps["_"].position

    namesym, type_expr = _define_name_type_pos_from_match(caps)
    name = str(namesym)

    frag = compiler.fragment(called_by)

    if "body" in caps:
        frag.declare_var(name, position=pos)
        formals = caps["formals"]
        body = caps["body"]
        if isinstance(formals, Datum):
            formals_spine = formals.datum
        else:
            formals_spine = formals.source
        body_spine = body.datum
        if type_expr is not None:
            val_expr = cons(
                symbol("compile/lambda"),
                namesym,
                keyword("::"),
                type_expr,
                formals_spine,
                body_spine)
        else:
            val_expr = cons(symbol("compile/lambda"), namesym,
                            formals_spine,
                            body_spine)

    elif "value" in caps:
        if type_expr is not None:
            frag.declare_var(name, position=pos, type_expr=type_expr)
        else:
            frag.declare_var(name, position=pos)
        val_expr = caps["value"].datum

    else:
        if type_expr is not None:
            frag.declare_var(name, position=pos, type_expr=type_expr)
        else:
            frag.declare_var(name, position=pos)
        frag.instruction(OpCode.KOS_OP_CONST, None)
        return frag

    if not is_pair(val_expr) and "value" in caps:
        frag.position(caps["value"].position)

    frag.merge(compiler.full_compile(val_expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@builtin("compile/globals")
@special("compile/globals", patterns=_DEFAULT_PATTERNS)
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


@builtin("compile/locals")
@special("compile/locals", patterns=_DEFAULT_PATTERNS)
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


@builtin("compile/define-global")
@special("compile/define-global", patterns=_DEFINE_PATTERNS, unpack="match")
def special_define_global(compiler, caps, tc=False):
    """
    Compile (define-global X), (define-global X EXPR),
    (define-global X [Y Z] BODY...), or typed ``X :: T`` / ``X type: T``
    variants. Explicit module binding for read and write.
    """

    called_by = caps["_"].datum

    namesym, type_expr = _define_name_type_pos_from_match(caps)
    name = str(namesym)

    frag = compiler.fragment(called_by)
    try:
        frag.request_global(name)
    except CompilerError:
        raise SpecialError(
            '%r cannot be both local (define) and global (define-global).'
            % (name,),
            called_by) from None

    if "body" in caps:
        formals = caps["formals"]
        body = caps["body"]
        if isinstance(formals, Datum):
            formals_spine = formals.datum
        else:
            formals_spine = formals.source
        body_spine = body.datum
        if type_expr is not None:
            val_expr = cons(
                symbol("compile/lambda"),
                namesym,
                keyword("::"),
                type_expr,
                formals_spine,
                body_spine)
        else:
            val_expr = cons(symbol("compile/lambda"), namesym,
                            formals_spine,
                            body_spine)

    elif "value" in caps:
        val_expr = caps["value"].datum

    else:
        frag.instruction(OpCode.KOS_OP_CONST, None)
        return frag

    if not is_pair(val_expr) and "value" in caps:
        frag.position(caps["value"].position)

    frag.merge(compiler.full_compile(val_expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
