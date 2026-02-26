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
:ai-assistant: Claude Opus 4.5 via Cursor
"""


from ...lib import car, cdr, cons, keyword, nil, OpCode, symbol
from ...types import fill_position, get_position, is_keyword, is_pair, unpack
from ...exceptions import SpecialError
from ..helpers import helper_begin, helper_binding, pycons, special


_keyword_except = keyword("except")
_keyword_finally = keyword("finally")


def _collect_catches(compiler, catches):
    """
    Parse try catch list into normal_catches, act_else, act_finally.

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
        if is_pair(ex) and ex.is_proper():
            ex_binding, ex_expr = helper_binding(ex, required=True, allow_unnamed=True)
            ex_pair = cons(cons(ex_binding, ex_expr), act)
            pos = get_position(ca)
            if pos:
                fill_position(ex_pair, pos)
            normal_catches.append(ex_pair)

        elif ex is _keyword_except:
            if act is nil or not is_pair(act):
                raise SpecialError("malformed except: clause in try", ca)
            ex_type = car(act)
            act_body = cdr(act)
            ex_pair = cons(cons(ex, ex_type), act_body)
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
            raise SpecialError("missing keyword label in else/finally", ca)

    return (normal_catches, act_else, act_finally)


@special(symbol("compile/check-exception"))
def special_check_exception(compiler, source, tc=False):
    called_by, checks = source

    fragment = compiler.fragment(called_by)
    count = 0

    while checks is not nil:
        expr, checks = checks
        fragment.merge(compiler.full_compile(expr, False))
        count += 1

    fragment.instruction(OpCode.KOS_OP_EXCEPTION_MATCH, count)
    return fragment


@special(symbol("compile/clear-exception"))
def special_clear_exception(compiler, source, tc=False):
    called_by, rest = source

    if rest is not nil:
        raise TypeError("too many arguments to clear-exception")

    fragment = compiler.fragment(called_by)
    fragment.instruction(OpCode.KOS_OP_EXCEPTION_CLEAR, 0)

    return fragment


@special(symbol("compile/load-exception"))
def special_load_exception(compiler, source, tc=False):
    called_by, rest = source

    if rest is not nil:
        raise TypeError("too many arguments to %s" % called_by)

    fragment = compiler.fragment(called_by)
    fragment.instruction(OpCode.KOS_OP_EXCEPTION, 0)

    return fragment


@special(symbol("compile/try"))
def special_try(compiler, source, tc=False):
    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("try requires body and clauses", source)

    try_expr, catches = rest
    ca, act_else, act_finally = _collect_catches(compiler, catches)

    if act_finally is not None:
        if act_else is not None:
            return _try_else_finally(
                compiler, try_expr, ca, act_else, act_finally, tc)
        else:
            return _try_finally(compiler, try_expr, ca, act_finally, tc)

    elif act_else is not None:
        return _try_else(compiler, try_expr, ca, act_else, tc)

    else:
        return _try(compiler, try_expr, ca, tc)


def _try(compiler, try_expr, catches, tc):
    except_label = compiler.gen_label()
    end_label = compiler.gen_label()

    frag = compiler.fragment("<try>")

    frag.instruction(OpCode.KOS_OP_PUSH_TRY, except_label)
    frag.merge(compiler.full_compile(try_expr, False))

    frag.instruction(OpCode.KOS_OP_CLEAR_TRY, 0)
    frag.instruction(OpCode.KOS_OP_JUMP, end_label)

    frag.instruction(OpCode.KOS_OP_LABEL, except_label)
    frag.merge(_except(compiler, catches))

    frag.instruction(OpCode.KOS_OP_LABEL, end_label)
    frag.instruction(OpCode.KOS_OP_POP_TRY, 1)

    return frag


def _try_else(compiler, try_expr, catches, else_exprs, tc):
    except_label = compiler.gen_label()
    end_label = compiler.gen_label()

    frag = compiler.fragment("<try-else>")

    frag.instruction(OpCode.KOS_OP_PUSH_TRY, except_label)
    frag.merge(compiler.full_compile(try_expr, False))

    frag.instruction(OpCode.KOS_OP_CLEAR_TRY, 0)
    helper_begin(frag, else_exprs, tc)
    frag.instruction(OpCode.KOS_OP_JUMP, end_label)

    frag.instruction(OpCode.KOS_OP_LABEL, except_label)
    frag.merge(_except(compiler, catches))

    frag.instruction(OpCode.KOS_OP_LABEL, end_label)
    frag.instruction(OpCode.KOS_OP_POP_TRY, 1)

    return frag


def _try_finally(compiler, try_expr, catches, fin_exprs, tc):
    cleanup = compiler.gen_label()

    frag = compiler.fragment("<try-finally>")

    frag.instruction(OpCode.KOS_OP_PUSH_FINALLY, cleanup)
    if catches:
        frag.merge(_try(compiler, try_expr, catches, tc))
    else:
        frag.merge(compiler.full_compile(try_expr, False))

    frag.instruction(OpCode.KOS_OP_CLEAR_FINALLY, 0)
    frag.instruction(OpCode.KOS_OP_LABEL, cleanup)
    helper_begin(frag, fin_exprs, False)

    frag.instruction(OpCode.KOS_OP_POP_FINALLY, 1)

    return frag


def _try_else_finally(
        compiler,
        try_expr,
        catches,
        else_exprs,
        fin_exprs,
        tc):

    cleanup = compiler.gen_label()

    frag = compiler.fragment("<try-else-finally>")

    frag.instruction(OpCode.KOS_OP_PUSH_FINALLY, cleanup)
    frag.merge(_try_else(compiler, try_expr, catches, else_exprs, False))

    frag.instruction(OpCode.KOS_OP_CLEAR_FINALLY, 0)
    frag.instruction(OpCode.KOS_OP_LABEL, cleanup)
    helper_begin(frag, fin_exprs, False)

    frag.instruction(OpCode.KOS_OP_POP_FINALLY, 1)

    return frag


def _except(compiler, catches):
    mod = [symbol("compile/cond")]

    for ca in catches:
        key_match, act = ca
        if is_pair(key_match):
            key = car(key_match)
            match = cdr(key_match)
        else:
            key = key_match
            match = nil

        if key is _keyword_except:
            key = None

        if is_pair(match):
            checksrc = cons(symbol("check-exception"), match)
        else:
            checksrc = cons(symbol("check-exception"), cons(match, nil))

        pos = get_position(ca)
        if pos:
            fill_position(checksrc, pos)

        act_list = list(unpack(act))
        if key is not None:
            define_form = cons(
                symbol("compile/define"),
                cons(symbol(str(key)), cons(cons(symbol("load-exception"), nil), nil)))
            begin_body = cons(define_form, act if is_pair(act) else cons(act, nil))
            mod.append((checksrc, cons(symbol("compile/begin"), begin_body)))
        else:
            mod.append((checksrc,) + tuple(act_list) if act_list else (checksrc, nil))

    mod.append((keyword("else"), cons(symbol("raise"), nil)))
    expr = pycons(mod)

    return compiler.full_compile(expr, False)


@special(symbol("compile/raise"))
def special_raise(compiler, source, tc=False):
    called_by, rest = source

    fragment = compiler.fragment(called_by)
    if rest is nil:
        fragment.instruction(OpCode.KOS_OP_RAISE, 0)
        return fragment

    expr, rest = rest
    if rest is not nil:
        raise TypeError("too many arguments to raise")

    fragment.merge(compiler.full_compile(expr, False))
    fragment.instruction(OpCode.KOS_OP_RAISE, 1)

    return fragment


# The end.
