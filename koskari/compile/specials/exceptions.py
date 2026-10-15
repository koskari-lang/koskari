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
koskari.compile.specials.exceptions

Special form handlers for try/except/finally, check-exception,
clear-exception, load-exception, and raise.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...lib import OpCode, cons, keyword, nil, symbol
from ...matcher import (
    CALLEDBY, CAPTURE, FORM, LIST, LITERAL_KW, MATCH, ONEOF, OPTIONAL,
    REST, USING_FORMS, VALUE, repeat_capture)
from ...syntax import special
from ...types import fill_position, get_position, is_nil, is_pair, unpack
from ..helpers import helper_begin, pycons


_NOARG_PATTERNS = [
    MATCH(CALLEDBY("called_by")),
]

_CHECKS_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("checks")),
]

# one except arm: the head is except: or a using cell, [TYPE],
# [NAME TYPE] or [TYPE as: NAME], then the handler forms
_EXCEPT_ARM = FORM(MATCH(
    ONEOF(CAPTURE("bare", LITERAL_KW("except")), *USING_FORMS),
    REST("body")))

_TRY_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        VALUE("body"),
        repeat_capture("catches", _EXCEPT_ARM),
        OPTIONAL(LIST(LITERAL_KW("else"), REST("else_body"))),
        OPTIONAL(LIST(LITERAL_KW("finally"), REST("finally_body")))),
]

# raise keeps a trailing REST rather than one arm per arity: a corpus
# record asserts its hand-written "too many arguments" message, which a
# match failure would replace
_RAISE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("rest")),
]


_keyword_except = keyword("except")


def _finally_body(compiler, fragment, body):
    """
    Compile ``finally:`` body for side effects only

    ``POP_FINALLY`` promotes one stack slot stashed under the finally block;
    discarding each expression's value keeps that slot as the try/except/else
    outcome or pending ``compile/return`` payload

    :param compiler: active Compiler
    :param fragment: bytecode fragment to merge into
    :param body: pair-list of finally expressions, or ``nil`` if empty
    :return: ``fragment``
    """

    if is_nil(body):
        return fragment

    while True:
        fragment.position_of(body)
        expr, body = body
        fragment.merge(compiler.full_compile(expr))
        fragment.instruction(OpCode.POP, 1)
        if is_nil(body):
            break

    return fragment


@builtin("compile/check-exception")
@special("compile/check-exception", patterns=_CHECKS_PATTERNS,
         unpack="kwargs")
def special_check_exception(compiler, called_by, checks):
    fragment = compiler.fragment(called_by.datum)
    count = 0

    for expr in checks.datum.unpack():
        fragment.merge(compiler.full_compile(expr))
        count += 1

    fragment.instruction(OpCode.BUILD_ARGS, count)
    fragment.instruction(OpCode.EXCEPTION_MATCH, 0)
    return fragment


@builtin("compile/clear-exception")
@special("compile/clear-exception", patterns=_NOARG_PATTERNS,
         unpack="kwargs")
def special_clear_exception(compiler, called_by):
    fragment = compiler.fragment(called_by.datum)
    fragment.instruction(OpCode.EXCEPTION_CLEAR, 0)

    return fragment


@builtin("compile/load-exception")
@special("compile/load-exception", patterns=_NOARG_PATTERNS,
         unpack="kwargs")
def special_load_exception(compiler, called_by):
    fragment = compiler.fragment(called_by.datum)
    fragment.instruction(OpCode.EXCEPTION, 0)

    return fragment


def _catch_rows(catches):
    """
    The except arms as (HEAD . BODY) cells for _except, HEAD a pair:
    (except: . nil) for a bare arm, (except: . TYPEEXPR) for a type-only
    arm, (SYM . TYPEEXPR) for a bound one. Each cell carries its arm's
    position so diagnostics inside a handler name the arm

    :param catches: the matched arm rows, one dict per arm
    :return: list of cells
    """

    rows = []
    for arm in catches:
        bare = arm.get("bare")
        if bare is not None:
            head = cons(_keyword_except, nil)
            pos = bare.position
        else:
            name = arm.get("name")
            expr = arm["expr"]
            head = cons(_keyword_except if name is None else name.datum,
                        expr.datum)
            pos = expr.position
        cell = cons(head, arm["body"].datum)
        if pos:
            fill_position(cell, pos)
        rows.append(cell)
    return rows


@builtin("compile/try")
@special("compile/try", patterns=_TRY_PATTERNS, unpack="kwargs")
def special_try(
        compiler,
        called_by,
        body,
        catches=(),
        else_body=None,
        finally_body=None):
    """
    Compile (try BODY ARM... [(else: BODY...)] [(finally: BODY...)]),
    where each ARM is (except: BODY...), ([TYPEEXPR] BODY...),
    ([SYM TYPEEXPR] BODY...) or ([TYPEEXPR as: SYM] BODY...)
    """

    try_expr = body.datum
    ca = _catch_rows(catches)
    act_else = None if else_body is None else else_body.datum
    act_finally = None if finally_body is None else finally_body.datum

    if act_finally is not None:
        if act_else is not None:
            return _try_else_finally(
                compiler, try_expr, ca, act_else, act_finally)
        else:
            return _try_finally(compiler, try_expr, ca, act_finally)

    elif act_else is not None:
        return _try_else(compiler, try_expr, ca, act_else)

    else:
        return _try(compiler, try_expr, ca)


def _try(compiler, try_expr, catches):
    except_label = compiler.gen_label()
    end_label = compiler.gen_label()

    frag = compiler.fragment("<try>")

    frag.instruction(OpCode.PUSH_TRY, except_label)
    frag.merge(compiler.full_compile(try_expr))

    frag.instruction(OpCode.CLEAR_TRY, 0)
    frag.instruction(OpCode.JUMP, end_label)

    frag.instruction(OpCode.LABEL, except_label)
    frag.merge(_except(compiler, catches))

    frag.instruction(OpCode.LABEL, end_label)
    frag.instruction(OpCode.POP_TRY, 1)

    return frag


def _try_else(compiler, try_expr, catches, else_exprs):
    except_label = compiler.gen_label()
    end_label = compiler.gen_label()

    frag = compiler.fragment("<try-else>")

    frag.instruction(OpCode.PUSH_TRY, except_label)
    frag.merge(compiler.full_compile(try_expr))

    frag.instruction(OpCode.CLEAR_TRY, 0)
    helper_begin(frag, else_exprs)
    frag.instruction(OpCode.JUMP, end_label)

    frag.instruction(OpCode.LABEL, except_label)
    frag.merge(_except(compiler, catches))

    frag.instruction(OpCode.LABEL, end_label)
    frag.instruction(OpCode.POP_TRY, 1)

    return frag


def _try_finally(compiler, try_expr, catches, fin_exprs):
    cleanup = compiler.gen_label()

    frag = compiler.fragment("<try-finally>")

    frag.instruction(OpCode.PUSH_FINALLY, cleanup)
    if catches:
        frag.merge(_try(compiler, try_expr, catches))
    else:
        frag.merge(compiler.full_compile(try_expr))

    frag.instruction(OpCode.CLEAR_FINALLY, 0)
    frag.instruction(OpCode.LABEL, cleanup)
    _finally_body(compiler, frag, fin_exprs)

    frag.instruction(OpCode.POP_FINALLY, 1)

    return frag


def _try_else_finally(
        compiler,
        try_expr,
        catches,
        else_exprs,
        fin_exprs):

    cleanup = compiler.gen_label()

    frag = compiler.fragment("<try-else-finally>")

    frag.instruction(OpCode.PUSH_FINALLY, cleanup)
    frag.merge(_try_else(compiler, try_expr, catches, else_exprs))

    frag.instruction(OpCode.CLEAR_FINALLY, 0)
    frag.instruction(OpCode.LABEL, cleanup)
    _finally_body(compiler, frag, fin_exprs)

    frag.instruction(OpCode.POP_FINALLY, 1)

    return frag


def _except(compiler, catches):
    mod = [symbol("cond")]
    clause_positions = [None]
    has_bare = False

    for ca in catches:
        key_match, act = ca
        key, match = key_match

        pos = get_position(ca)

        if key is _keyword_except and is_nil(match):
            has_bare = True
            act_list = list(unpack(act))
            mod.append(
                (keyword("else"),) + tuple(act_list)
                if act_list
                else (keyword("else"), nil))
            clause_positions.append(pos)

        elif key is _keyword_except:
            checksrc = cons(symbol("check-exception"), cons(match, nil))
            if pos:
                fill_position(checksrc, pos)
            act_list = list(unpack(act))
            mod.append(
                (checksrc,) + tuple(act_list)
                if act_list
                else (checksrc, nil))
            clause_positions.append(pos)

        else:
            checksrc = cons(symbol("check-exception"), cons(match, nil))
            if pos:
                fill_position(checksrc, pos)
            define_form = cons(
                symbol("define"),
                symbol(str(key)),
                cons(symbol("load-exception"), nil),
                nil)
            if is_pair(act):
                begin_body = cons(define_form, act)
            else:
                begin_body = cons(define_form, act, nil)
            begin_form = cons(symbol("begin"), begin_body)
            if pos:
                fill_position(begin_form, pos)
            mod.append((checksrc, begin_form))
            clause_positions.append(pos)

    if not has_bare:
        mod.append((keyword("else"), cons(symbol("raise"), nil)))
        clause_positions.append(None)

    expr = pycons(mod)

    # pycons builds fresh positionless conses; restamp each clause with
    # the catch it came from so diagnostics inside handlers name it
    spine = expr.cdr
    for pos in clause_positions[1:]:
        clause, spine = spine
        if pos:
            fill_position(clause, pos)

    return compiler.full_compile(expr)


@builtin("compile/raise")
@special("compile/raise", patterns=_RAISE_PATTERNS, unpack="kwargs")
def special_raise(compiler, called_by, rest):
    body = rest.datum

    fragment = compiler.fragment(called_by.datum)
    if is_nil(body):
        fragment.instruction(OpCode.RAISE, 0)
        return fragment

    expr, tail = body
    if not is_nil(tail):
        raise TypeError("too many arguments to raise")

    fragment.merge(compiler.full_compile(expr))
    fragment.instruction(OpCode.RAISE, 1)

    return fragment


# The end.
