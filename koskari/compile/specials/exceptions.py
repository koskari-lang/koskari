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
from ...exceptions import SpecialError
from ...lib import OpCode, cons, keyword, nil, symbol
from ...matcher import CALLEDBY, MATCH, REST
from ...syntax import special
from ...types import (
    fill_position, get_position, is_keyword, is_nil, is_pair, unpack)
from ..helpers import helper_begin, helper_using, pycons


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


_keyword_except = keyword("except")
_keyword_finally = keyword("finally")


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


def _collect_catches(compiler, catches):
    """
    Parse try catch list into normal_catches, act_else, act_finally.

    Each element of catches must be (EXCEPTMATCH BODY...) where EXCEPTMATCH
    is one of:

    - keyword("except") — bare catch-all, no type check, no binding
    - TYPEEXPR (any non-keyword, non-pair atom) — type-only, no binding
    - (TYPEEXPR), (SYM TYPEEXPR), or (TYPEEXPR as: SYM) — USE form

    Normal catches are normalised to (HEAD . BODY) where HEAD is a pair:

    - (except: . nil) for bare
    - (except: . TYPEEXPR) for type-only
    - (SYM . TYPEEXPR) for bound

    :param compiler: active Compiler
    :param catches: pair list of clauses
    :return: (normal_catches, act_else, act_finally)
    :raises SpecialError: on invalid clauses
    """

    normal_catches = []
    act_else = None
    act_finally = None

    for ca in unpack(catches):
        if not ca:
            raise SpecialError("clause with no body in try", ca)

        ex, act = ca

        if ex is _keyword_except:
            ex_pair = cons(cons(_keyword_except, nil), act)
            pos = get_position(ca)
            if pos:
                fill_position(ex_pair, pos)
            normal_catches.append(ex_pair)

        elif is_pair(ex) and ex.is_proper():
            ex_binding, ex_expr = helper_using(ex)
            # helper_using reports "no binding" as None; normalise to the
            # except: marker so a type-only clause takes the unbound path
            # and does not compile a binding named "None"
            if ex_binding is None:
                ex_binding = _keyword_except
            ex_pair = cons(cons(ex_binding, ex_expr), act)
            pos = get_position(ca)
            if pos:
                fill_position(ex_pair, pos)
            normal_catches.append(ex_pair)

        elif not is_keyword(ex):
            ex_pair = cons(cons(_keyword_except, ex), act)
            pos = get_position(ca)
            if pos:
                fill_position(ex_pair, pos)
            normal_catches.append(ex_pair)

        elif ex is _keyword_finally:
            if act_finally is not None:
                raise SpecialError("duplicate finally: clause in try", ca)
            act_finally = act

        elif is_keyword(ex) and str(ex) == "else":
            if act_else is not None:
                raise SpecialError("duplicate else: clause in try", ca)
            act_else = act

        else:
            raise SpecialError("unrecognised clause keyword in try: %r" % ex, ca)

    return (normal_catches, act_else, act_finally)


@builtin("compile/check-exception")
@special("compile/check-exception", patterns=_DEFAULT_PATTERNS)
def special_check_exception(compiler, source):
    called_by, checks = source

    fragment = compiler.fragment(called_by)
    count = 0

    while not is_nil(checks):
        expr, checks = checks
        fragment.merge(compiler.full_compile(expr))
        count += 1

    fragment.instruction(OpCode.BUILD_ARGS, count)
    fragment.instruction(OpCode.EXCEPTION_MATCH, 0)
    return fragment


@builtin("compile/clear-exception")
@special("compile/clear-exception", patterns=_DEFAULT_PATTERNS)
def special_clear_exception(compiler, source):
    called_by, rest = source

    if not is_nil(rest):
        raise TypeError("too many arguments to clear-exception")

    fragment = compiler.fragment(called_by)
    fragment.instruction(OpCode.EXCEPTION_CLEAR, 0)

    return fragment


@builtin("compile/load-exception")
@special("compile/load-exception", patterns=_DEFAULT_PATTERNS)
def special_load_exception(compiler, source):
    called_by, rest = source

    if not is_nil(rest):
        raise TypeError("too many arguments to %s" % called_by)

    fragment = compiler.fragment(called_by)
    fragment.instruction(OpCode.EXCEPTION, 0)

    return fragment


@builtin("compile/try")
@special("compile/try", patterns=_DEFAULT_PATTERNS)
def special_try(compiler, source):
    called_by, rest = source
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError("try requires body and clauses", source)

    try_expr, catches = rest
    ca, act_else, act_finally = _collect_catches(compiler, catches)

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
@special("compile/raise", patterns=_DEFAULT_PATTERNS)
def special_raise(compiler, source):
    called_by, rest = source

    fragment = compiler.fragment(called_by)
    if is_nil(rest):
        fragment.instruction(OpCode.RAISE, 0)
        return fragment

    expr, rest = rest
    if not is_nil(rest):
        raise TypeError("too many arguments to raise")

    fragment.merge(compiler.full_compile(expr))
    fragment.instruction(OpCode.RAISE, 1)

    return fragment


# The end.
