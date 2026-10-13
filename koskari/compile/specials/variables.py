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
compile/define-values, compile/delq, compile/define, compile/var,
compile/global, and compile/globals.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...hygiene import binding_atom
from ...lib import OpCode, cons, symbol
from ...matcher import (
    CALLEDBY, DEFINE, LITERAL_KW, MATCH, PAIR, REST, SYMBOL, VALUE)
from ...syntax import special
from ...types import is_pair, is_symbol
from ..helpers import unpack_lhs_bind_symbols, using_unpack_helper


def name_and_type(matched):
    """
    The name symbol and optional type expression of a var-shaped match,
    whose name is the DEFINE slot and whose type, when present, follows
    ``::`` or ``type:``
    """

    rt = matched.get("return_type")
    return matched["name"].datum, (rt.datum if rt else None)


_SETQ_PATTERNS = [
    MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr")),
]


@builtin("compile/setq")
@special("compile/setq", patterns=_SETQ_PATTERNS, unpack="match")
def special_setq(compiler, caps):
    """
    Compile (compile/setq SYM EXPR). Store the value of EXPR into the existing
    binding SYM
    """

    symatom = caps["sym"].datum
    # a symbol read under a layout block carries no position of its
    # own; the form's names the line
    pos = caps["sym"].position or caps["called-by"].position
    expr = caps["expr"].datum

    frag = compiler.fragment(caps["called-by"].datum)

    if not is_pair(expr):
        frag.position(pos)

    frag.request_for_write(symatom, position=pos)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.VAR_STORE, symatom)
    frag.instruction(OpCode.CONST, None)

    return frag


_VALUES_BIND_PATTERNS = [
    MATCH(CALLEDBY(), SYMBOL("lhs"), VALUE("expr")),
    MATCH(CALLEDBY(), PAIR("lhs"), VALUE("expr")),
]


@builtin("compile/setq-values")
@special("compile/setq-values", patterns=_VALUES_BIND_PATTERNS, unpack="match")
def special_setq_values(compiler, caps):
    """
    Compile (compile/setq-values PATTERN VALUE). Evaluate VALUE, then store
    into existing bindings given by PATTERN (symbol or unpack list, same as
    compile/for-each unpack binding)
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    lhs_mr = caps["lhs"]
    expr_mr = caps["expr"]

    frag = compiler.fragment(called_by)

    lhs = lhs_mr.datum

    for namesym in unpack_lhs_bind_symbols(lhs, flatten=True):
        frag.request_for_write(namesym, position=pos)

    frag.position(expr_mr.position)
    frag.merge(compiler.full_compile(expr_mr.datum))
    frag.merge(
        using_unpack_helper(compiler, lhs, declare=False))
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/define-values")
@special("compile/define-values", patterns=_VALUES_BIND_PATTERNS, unpack="match")
def special_define_values(compiler, caps):
    """
    Compile (compile/define-values PATTERN VALUE). Evaluate VALUE, then bind
    fresh names from PATTERN (symbol or unpack list, same shape as
    compile/setq-values)
    """

    called_by = caps["called-by"].datum

    lhs_mr = caps["lhs"]
    expr_mr = caps["expr"]

    frag = compiler.fragment(called_by)

    lhs = lhs_mr.datum

    frag.position(expr_mr.position)
    frag.merge(compiler.full_compile(expr_mr.datum))
    frag.merge(
        using_unpack_helper(compiler, lhs, declare=True))
    frag.instruction(OpCode.CONST, None)

    return frag


_DELQ_PATTERNS = [
    MATCH(CALLEDBY(), SYMBOL("sym")),
]


@builtin("compile/delq")
@special("compile/delq", patterns=_DELQ_PATTERNS, unpack="match")
def special_delq(compiler, caps):
    """
    Compile (compile/delq SYM). Clear the binding SYM, resetting it to unset
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position
    sym = caps["sym"].datum

    frag = compiler.fragment(called_by)

    frag.position(pos)
    frag.request_for_write(sym, position=pos)
    frag.instruction(OpCode.VAR_CLEAR, sym)
    frag.instruction(OpCode.CONST, None)

    return frag


_TAKEQ_PATTERNS = [
    MATCH(CALLEDBY(), SYMBOL("sym")),
]


@builtin("compile/takeq")
@special("compile/takeq", patterns=_TAKEQ_PATTERNS, unpack="match")
def special_takeq(compiler, caps):
    """
    Compile (compile/takeq SYM). Deref SYM, clear the binding, push value
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position
    sym = caps["sym"].datum

    frag = compiler.fragment(called_by)

    frag.position(pos)
    frag.request_for_write(sym, position=pos)
    frag.instruction(OpCode.VAR_TAKE, sym)

    return frag


def emit_value(compiler, frag, caps, namesym):
    """
    Finish a var-shaped fragment from its match: a value compiles
    itself (positioned at an atom value) and is stored to the name, a
    declaration compiles nothing, and the form's result is None

    :return: the fragment
    """

    if "value" in caps:
        val_expr = caps["value"].datum
        if not is_pair(val_expr):
            frag.position(caps["value"].position)
        frag.merge(compiler.full_compile(val_expr))
        frag.instruction(OpCode.VAR_STORE, namesym)

    frag.instruction(OpCode.CONST, None)
    return frag


def emit_function(compiler, frag, head, body, namesym):
    """
    Finish a define-shaped fragment whose name position holds
    (NAME FORMALS...): compile (function NAME FORMALS BODY...) and store
    it to the name. The form's result is None

    :return: the fragment
    """

    _name, formals = head
    val_expr = cons(symbol("function"), namesym, formals, body)
    frag.merge(compiler.full_compile(val_expr))
    frag.instruction(OpCode.VAR_STORE, namesym)
    frag.instruction(OpCode.CONST, None)
    return frag


# the value and declaration shapes: a name and a value, never formals
# and a body
_VAR_PATTERNS = [
    MATCH(
        CALLEDBY(),
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type"),
        VALUE("value")),
    MATCH(
        CALLEDBY(),
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type"),
        VALUE("value")),
    MATCH(
        CALLEDBY(),
        DEFINE("name"),
        LITERAL_KW("::"),
        VALUE("return_type")),
    MATCH(
        CALLEDBY(),
        DEFINE("name"),
        LITERAL_KW("type:"),
        VALUE("return_type")),
    MATCH(CALLEDBY(), DEFINE("name"), VALUE("value")),
    MATCH(CALLEDBY(), DEFINE("name")),
]


# Scheme's shape: a pair in the name position is the name and the
# formals of a function, a symbol is a value binding
_DEFINE_PATTERNS = [
    MATCH(CALLEDBY(), PAIR("head"), REST("body")),
] + _VAR_PATTERNS


def scheme_function_name(caps):
    """
    The name symbol heading the pair in a define's name position

    :raises SpecialError: when the pair is not headed by a symbol
    """

    head = caps["head"].datum
    name, _formals = head
    if not is_symbol(name):
        raise SpecialError(
            "define expects a name to head (NAME FORMALS ...)",
            head, position=caps["called-by"].position)
    return name


def compile_var(compiler, caps):
    """
    Declare a var-shaped match's name in the current scope and store its
    value

    :return: the fragment
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    namesym, type_expr = name_and_type(caps)
    namesym = binding_atom(compiler, namesym)

    frag = compiler.fragment(called_by)
    if type_expr is None:
        frag.declare_var(namesym, position=pos)
    else:
        frag.declare_var(namesym, position=pos, type_expr=type_expr)

    return emit_value(compiler, frag, caps, namesym)


def compile_global(compiler, caps):
    """
    Bind a var-shaped match's name at the module and store its value

    :return: the fragment
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    namesym, _type_expr = name_and_type(caps)

    frag = compiler.fragment(called_by)
    # the local-or-global conflict is judged where this fragment merges
    # into its body, the first point at which a declaration of the
    # same name is in view
    frag.request_global(namesym, position=pos)

    return emit_value(compiler, frag, caps, namesym)


@builtin("compile/var")
@special("compile/var", patterns=_VAR_PATTERNS, unpack="match")
def special_var(compiler, caps):
    """
    Compile (var X), (var X EXPR), or the typed variants with
    ``X :: T`` / ``X type: T`` before the value or alone. Declares X in
    the scope the form appears in and stores the value
    """

    return compile_var(compiler, caps)


@builtin("compile/global")
@special("compile/global", patterns=_VAR_PATTERNS, unpack="match")
def special_global(compiler, caps):
    """
    Compile (global X), (global X EXPR), or the typed variants with
    ``X :: T`` / ``X type: T``. Binds X at the module from any depth and
    stores the value
    """

    return compile_global(compiler, caps)


@builtin("compile/define")
@special("compile/define", patterns=_DEFINE_PATTERNS, unpack="match")
def special_define(compiler, caps):
    """
    Compile (define (F ARGS...) BODY...), the function F of ARGS bound
    in the current scope, or (define X), (define X EXPR) and their typed
    variants, which are var's
    """

    if "head" not in caps:
        return compile_var(compiler, caps)

    namesym = binding_atom(compiler, scheme_function_name(caps))
    frag = compiler.fragment(caps["called-by"].datum)
    frag.declare_var(namesym, position=caps["called-by"].position)
    return emit_function(
        compiler, frag, caps["head"].datum, caps["body"].datum, namesym)


@builtin("compile/define-global")
@special("compile/define-global", patterns=_DEFINE_PATTERNS, unpack="match")
def special_define_global(compiler, caps):
    """
    Compile (define-global (F ARGS...) BODY...), the function F of ARGS
    bound at the module, or (define-global X), (define-global X EXPR)
    and their typed variants, which are global's
    """

    if "head" not in caps:
        return compile_global(compiler, caps)

    namesym = scheme_function_name(caps)
    frag = compiler.fragment(caps["called-by"].datum)
    frag.request_global(namesym, position=caps["called-by"].position)
    return emit_function(
        compiler, frag, caps["head"].datum, caps["body"].datum, namesym)


_NOARG_PATTERNS = [
    MATCH(CALLEDBY("called_by")),
]


@builtin("compile/globals")
@special("compile/globals", patterns=_NOARG_PATTERNS, unpack="kwargs")
def special_globals(compiler, called_by):
    """
    Compile (compile/globals). Push the outermost frame namespace (globals)
    """

    frag = compiler.fragment(called_by.datum)
    frag.instruction(OpCode.GLOBALS, 0)
    return frag


# The end.
