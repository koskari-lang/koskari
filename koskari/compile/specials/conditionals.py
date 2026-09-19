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
from ...matcher import (
    BODY, CALLEDBY, FORM, MATCH, REST, VALUE, repeat_capture)
from ...syntax import special
from ...types import is_pair, strip_mark
from ..helpers import helper_begin


_keyword_else = keyword("else")
_keyword_when = keyword("when")
_keyword_unless = keyword("unless")

_CLAUSES = repeat_capture(
    "clauses", FORM(MATCH(VALUE("test"), BODY("body"))))

_COND_PATTERNS = [
    MATCH(CALLEDBY("called_by"), _CLAUSES),
]

_SWITCH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("value"), _CLAUSES),
]

_EXPRS_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("exprs")),
]


@builtin("compile/cond")
@special("compile/cond", patterns=_COND_PATTERNS, unpack="kwargs")
def special_cond(compiler, called_by, clauses):
    """
    Compile (compile/cond (TEST BODY...) (TEST BODY...) ... (else: BODY...)).
    Conditionally executes body depending on a test expression. Multiple
    clauses; first truthful test's body is evaluated and returned. No match
    yields None. The else: keyword as test is always true.
    """

    fragment = compiler.fragment(called_by.datum)

    gen_label = compiler.gen_label
    done = gen_label()
    label = gen_label()

    remaining = len(clauses)
    for clause in clauses:
        remaining -= 1

        testcap = clause["test"]
        test = testcap.datum
        body = clause["body"].datum
        clause_pos = testcap.position

        fragment.position(clause_pos)
        fragment.instruction(OpCode.LABEL, label)
        label = gen_label()

        if strip_mark(test) is _keyword_else:
            if remaining:
                raise SpecialError(
                    "else: must be last clause", test, position=clause_pos)

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
@special("compile/switch", patterns=_SWITCH_PATTERNS, unpack="kwargs")
def special_switch(compiler, called_by, value, clauses):
    """
    Compile (compile/switch VALUE (COMPARE BODY...) (COMPARE BODY...) ... (else: BODY...)).
    Conditionally executes body depending on a compare expression. Multiple
    clauses; first compare expression's body is evaluated and returned. No match
    evaluates as None. The else: keyword always matches.
    """

    fragment = compiler.fragment(called_by.datum)

    gen_label = compiler.gen_label
    done = gen_label()

    fragment.merge(compiler.full_compile(value.datum))

    checks = compiler.fragment("switch-cases")
    bodies = compiler.fragment("switch-bodies")

    remaining = len(clauses)
    for clause in clauses:
        remaining -= 1

        testcap = clause["test"]
        test = testcap.datum
        body = clause["body"].datum
        clause_pos = testcap.position

        checks.position(clause_pos)

        if strip_mark(test) is _keyword_else:
            if remaining:
                raise SpecialError(
                    "else: must be last clause", test, position=clause_pos)

            else_label = gen_label()
            checks.instruction(OpCode.JUMP, else_label)
            bodies.instruction(OpCode.LABEL, else_label)
            bodies.instruction(OpCode.POP, 1)
            helper_begin(bodies, body)
            bodies.instruction(OpCode.JUMP, done)
            break

        elif not (test and is_pair(test)):
            raise SpecialError(
                "switch compare must be a list of values",
                test, position=clause_pos)

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


def emit_shortcut(compiler, called_by, exprs, empty_value, jump_op):
    """
    A fragment for a short-circuit special: each expression in turn,
    jumping to the end over the rest when one settles the result. An
    empty form is the constant empty_value

    :return: the fragment
    """

    fragment = compiler.fragment(called_by.datum)

    cl = exprs.datum
    if not cl:
        fragment.instruction(OpCode.CONST, empty_value)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr))
        if cl:
            fragment.instruction(jump_op, done)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


@builtin("compile/and")
@special("compile/and", patterns=_EXPRS_PATTERNS, unpack="kwargs")
def special_and(compiler, called_by, exprs):
    """
    Compile (compile/and EXPR ...). Short-circuit conjunction. Evaluates
    expressions left-to-right; returns the first falsy value without evaluating
    further. If all values are truthy, returns the last value. Zero arguments
    yields True
    """

    return emit_shortcut(
        compiler, called_by, exprs, True, OpCode.JUMP_AND)


@builtin("compile/or")
@special("compile/or", patterns=_EXPRS_PATTERNS, unpack="kwargs")
def special_or(compiler, called_by, exprs):
    """
    Compile (compile/or EXPR ...). Short-circuit disjunction. Evaluates
    expressions left-to-right; returns the first truthy value without evaluating
    further. If all values are falsy, returns the last value. Zero arguments
    yields False
    """

    return emit_shortcut(
        compiler, called_by, exprs, False, OpCode.JUMP_OR)


# The end.
