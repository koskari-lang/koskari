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

Special form handlers for compile/and, compile/or, compile/cond, and
compile/switch.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode, keyword
from ...matcher import CALLEDBY, MATCH, REST
from ...syntax import special
from ...types import get_position, is_pair
from ..helpers import helper_begin


_keyword_else = keyword("else")
_keyword_when = keyword("when")
_keyword_unless = keyword("unless")

_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/cond")
@special("compile/cond", patterns=_DEFAULT_PATTERNS)
def special_cond(compiler, source):
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
        fragment.instruction(OpCode.LABEL, label)
        label = gen_label()

        test, body = element

        clause_pos = get_position(element)

        if test is _keyword_else:
            if cl:
                raise SpecialError("else: must be last clause", source)

            helper_begin(fragment, body, position=clause_pos)
            break

        fragment.merge(compiler.full_compile(test, position=clause_pos))
        fragment.instruction(OpCode.JUMP_FALSE, label)
        helper_begin(fragment, body, position=clause_pos)
        fragment.instruction(OpCode.JUMP, done)

    else:
        fragment.instruction(OpCode.LABEL, label)
        fragment.instruction(OpCode.CONST, None)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


@builtin("compile/switch")
@special("compile/switch", patterns=_DEFAULT_PATTERNS)
def special_switch(compiler, source):
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
            checks.instruction(OpCode.JUMP, else_label)
            bodies.instruction(OpCode.LABEL, else_label)
            bodies.instruction(OpCode.POP, 1)
            helper_begin(bodies, body)
            bodies.instruction(OpCode.JUMP, done)
            break

        elif not (test and is_pair(test)):
            raise SpecialError("switch compare must be a list of values",
                               source)

        body_label = gen_label()
        while test:
            checks.position_of(test)
            tval, test = test

            checks.merge(compiler.full_compile(tval))
            checks.instruction(OpCode.SWITCH_EQ, body_label)

        bodies.instruction(OpCode.LABEL, body_label)
        helper_begin(bodies, body)
        bodies.instruction(OpCode.JUMP, done)

    else:
        body_label = gen_label()
        checks.instruction(OpCode.JUMP, body_label)
        bodies.instruction(OpCode.LABEL, body_label)
        bodies.instruction(OpCode.POP, 1)
        bodies.instruction(OpCode.CONST, None)

    fragment.merge(checks)
    fragment.merge(bodies)
    fragment.instruction(OpCode.LABEL, done)
    return fragment


@builtin("compile/and")
@special("compile/and", patterns=_DEFAULT_PATTERNS)
def special_and(compiler, source):
    """
    Compile (compile/and EXPR ...). Short-circuit conjunction. Evaluates
    expressions left-to-right; returns the first falsy value without evaluating
    further. If all values are truthy, returns the last value. Zero arguments
    yields True
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    if not cl:
        fragment.instruction(OpCode.CONST, True)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr))
        if cl:
            fragment.instruction(OpCode.JUMP_AND, done)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


@builtin("compile/or")
@special("compile/or", patterns=_DEFAULT_PATTERNS)
def special_or(compiler, source):
    """
    Compile (compile/or EXPR ...). Short-circuit disjunction. Evaluates
    expressions left-to-right; returns the first truthy value without evaluating
    further. If all values are falsy, returns the last value. Zero arguments
    yields False
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    if not cl:
        fragment.instruction(OpCode.CONST, False)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr))
        if cl:
            fragment.instruction(OpCode.JUMP_OR, done)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


# The end.
