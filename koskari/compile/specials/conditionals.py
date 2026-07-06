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
koskari.compile.specials.conditionals

Special form handlers for compile/and, compile/or, compile/cond, compile/if,
compile/when, and compile/unless.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...exceptions import SpecialError
from ...lib import OpCode, keyword
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...syntax import special
from ...types import is_pair
from ..helpers import helper_begin


_keyword_else = keyword("else")

_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/cond")
@special("compile/cond", patterns=_DEFAULT_PATTERNS)
def special_cond(compiler, source, tc=False):
    """
    Compile (compile/cond (TEST BODY...) (TEST BODY...) ... (else: BODY...)).
    Conditionally executes body depending on a test expression. Multiple
    clauses; first truthful test's body is evaluated and returned. No match
    yields None. The else: keyword as test is always true.
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    gen_label = compiler.gen_label
    done = gen_label()
    label = gen_label()

    while cl:
        element, cl = cl

        fragment.position_of(element)
        fragment.instruction(OpCode.KOS_OP_LABEL, label)
        label = gen_label()

        test, body = element

        if test is _keyword_else:
            if cl:
                raise SpecialError("else: must be last clause", source)

            helper_begin(fragment, body, tc)
            break

        fragment.merge(compiler.full_compile(test))
        fragment.instruction(OpCode.KOS_OP_JUMP_FALSE, label)
        helper_begin(fragment, body, tc)
        fragment.instruction(OpCode.KOS_OP_JUMP, done)

    else:
        fragment.instruction(OpCode.KOS_OP_LABEL, label)
        fragment.instruction(OpCode.KOS_OP_CONST, None)

    fragment.instruction(OpCode.KOS_OP_LABEL, done)
    return fragment


@builtin("compile/switch")
@special("compile/switch", patterns=_DEFAULT_PATTERNS)
def special_switch(compiler, source, tc=False):
    """
    Compile (compile/switch VALUE (COMPARE BODY...) (COMPARE BODY...) ... (else: BODY...)).
    Conditionally executes body depending on a compare expression. Multiple
    clauses; first compare expression's body is evaluated and returned. No match
    evaluates as None. The else: keyword always matches.
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    gen_label = compiler.gen_label
    done = gen_label()

    if not cl:
        raise SpecialError("switch requires a value", source)
    value, cl = cl

    fragment.merge(compiler.full_compile(value))

    checks = compiler.fragment("switch-cases")
    bodies = compiler.fragment("switch-bodies")

    while cl:
        element, cl = cl

        checks.position_of(element)
        test, body = element

        if test is _keyword_else:
            if cl:
                raise SpecialError("else: must be last clause", source)

            else_label = gen_label()
            checks.instruction(OpCode.KOS_OP_JUMP, else_label)
            bodies.instruction(OpCode.KOS_OP_LABEL, else_label)
            bodies.instruction(OpCode.KOS_OP_POP, 1)
            helper_begin(bodies, body, tc)
            bodies.instruction(OpCode.KOS_OP_JUMP, done)
            break

        elif not (test and is_pair(test)):
            raise SpecialError("switch compare must be a list of values",
                               source)

        body_label = gen_label()
        while test:
            checks.position_of(test)
            tval, test = test

            checks.merge(compiler.full_compile(tval))
            checks.instruction(OpCode.KOS_OP_SWITCH_EQ, body_label)

        bodies.instruction(OpCode.KOS_OP_LABEL, body_label)
        helper_begin(bodies, body, tc)
        bodies.instruction(OpCode.KOS_OP_JUMP, done)

    else:
        body_label = gen_label()
        checks.instruction(OpCode.KOS_OP_JUMP, body_label)
        bodies.instruction(OpCode.KOS_OP_LABEL, body_label)
        bodies.instruction(OpCode.KOS_OP_POP, 1)
        bodies.instruction(OpCode.KOS_OP_CONST, None)

    fragment.merge(checks)
    fragment.merge(bodies)
    fragment.instruction(OpCode.KOS_OP_LABEL, done)
    return fragment


@builtin("compile/and")
@special("compile/and", patterns=_DEFAULT_PATTERNS)
def special_and(compiler, source, tc=False):
    """
    Compile (compile/and EXPR ...). Short-circuit conjunction. Evaluates
    expressions left-to-right; returns the first falsy value without evaluating
    further. If all values are truthy, returns the last value. Zero arguments
    yields True
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    if not cl:
        fragment.instruction(OpCode.KOS_OP_CONST, True)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr, tc if not cl else False))
        if cl:
            fragment.instruction(OpCode.KOS_OP_JUMP_AND, done)

    fragment.instruction(OpCode.KOS_OP_LABEL, done)
    return fragment


@builtin("compile/or")
@special("compile/or", patterns=_DEFAULT_PATTERNS)
def special_or(compiler, source, tc=False):
    """
    Compile (compile/or EXPR ...). Short-circuit disjunction. Evaluates
    expressions left-to-right; returns the first truthy value without evaluating
    further. If all values are falsy, returns the last value. Zero arguments
    yields False
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    if not cl:
        fragment.instruction(OpCode.KOS_OP_CONST, False)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr, tc if not cl else False))
        if cl:
            fragment.instruction(OpCode.KOS_OP_JUMP_OR, done)

    fragment.instruction(OpCode.KOS_OP_LABEL, done)
    return fragment


# The end.
