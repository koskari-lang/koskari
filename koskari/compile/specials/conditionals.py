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
from ...lib import OpCode, bracket_nil, bracket_pair, keyword
from ...matcher import (
        CALLEDBY, FORM, MATCH, MatchCompiler, REST,
        parse_matcher, pattern_slot_names)
from ...bootstrap import builtin
from ...syntax import special
from ...types import is_pair
from ..helpers import helper_begin


_keyword_else = keyword("else")
_keyword_star = keyword("*")

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
        fragment.instruction(OpCode.LABEL, label)
        label = gen_label()

        test, body = element

        if test is _keyword_else:
            if cl:
                raise SpecialError("else: must be last clause", source)

            helper_begin(fragment, body, tc)
            break

        fragment.merge(compiler.full_compile(test))
        fragment.instruction(OpCode.JUMP_FALSE, label)
        helper_begin(fragment, body, tc)
        fragment.instruction(OpCode.JUMP, done)

    else:
        fragment.instruction(OpCode.LABEL, label)
        fragment.instruction(OpCode.CONST, None)

    fragment.instruction(OpCode.LABEL, done)
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
            checks.instruction(OpCode.JUMP, else_label)
            bodies.instruction(OpCode.LABEL, else_label)
            bodies.instruction(OpCode.POP, 1)
            helper_begin(bodies, body, tc)
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
        helper_begin(bodies, body, tc)
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


def _iter_forms(seq):
    """
    Yield the elements of a koskari list (pair or bracket-pair), stopping
    at either terminator (nil or bracket-nil, both falsy).
    """

    while seq:
        element, seq = seq
        yield element


def _lower_match_element(element, source):
    """
    Lower one scalar case-pattern element through the shared matcher
    surface parser.
    """

    try:
        return parse_matcher(element)
    except ValueError as exc:
        raise SpecialError(str(exc), source)


def _lower_match_sequence(seq, source):
    """
    Lower a ``[...]`` sequence pattern into ``FORM(MATCH(...))``. A ``*:``
    marker binds the remaining elements as a rest capture; a nested
    bracket list is a nested sequence; every other element is matcher
    surface syntax.
    """

    elements = []
    forms = _iter_forms(seq)
    for element in forms:
        if element is _keyword_star:
            name = next(forms, None)
            if name is None:
                raise SpecialError("*: must be followed by a name", source)
            elements.append(REST(str(name)))
        elif isinstance(element, bracket_pair) or element is bracket_nil:
            elements.append(_lower_match_sequence(element, source))
        else:
            elements.append(_lower_match_element(element, source))
    return FORM(MATCH(*elements))


def _lower_match_pattern(pattern, source):
    """
    Lower one case pattern. A bracket list is a sequence pattern matched
    against the whole subject; anything else is a single matcher element.
    """

    if isinstance(pattern, bracket_pair) or pattern is bracket_nil:
        return _lower_match_sequence(pattern, source)
    return _lower_match_element(pattern, source)


@builtin("compile/match")
@special("compile/match", patterns=_DEFAULT_PATTERNS)
def special_match(compiler, source, tc=False):
    """
    Compile (compile/match SUBJECT [PATTERN BODY...] ... [else: BODY...]).
    Evaluates SUBJECT once, then tries each arm's pattern in order. The
    first matching arm binds its captured names and runs its body, whose
    value is the result. A [else: BODY...] arm always matches; with no
    else and no match the form yields None.
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    gen_label = compiler.gen_label
    done = gen_label()

    if not cl:
        raise SpecialError("match requires a subject", source)
    subject, cl = cl

    fragment.merge(compiler.full_compile(subject))

    declared = set()

    while cl:
        element, cl = cl

        fragment.position_of(element)

        if not is_pair(element):
            raise SpecialError("match clause must be a list", source)
        pattern, body = element

        if pattern is _keyword_else:
            if cl:
                raise SpecialError("else: must be last clause", source)

            fragment.instruction(OpCode.POP, 1)
            helper_begin(fragment, body, tc)
            fragment.instruction(OpCode.JUMP, done)
            break

        arm = MATCH(_lower_match_pattern(pattern, source))
        frozen = MatchCompiler().lower(arm)
        names = [str(n) for n in pattern_slot_names(arm)]
        externs = tuple(str(name) for name in frozen[2])

        # the matcher resolves its externs (type / class / predicate
        # names) from the namespace at match time, so request each one
        # through resolution -- otherwise a closure-bound type used only
        # in a pattern is never captured.
        for extern in externs:
            fragment.request_var(extern)

        next_label = gen_label()
        fragment.instruction(OpCode.MATCH_CASE,
                             (next_label, frozen, externs, 0))

        for name in names:
            if name not in declared:
                declared.add(name)
                fragment.declare_var(name)

        if names:
            fragment.instruction(OpCode.MATCH_STORE_NAMES, tuple(names))
        else:
            fragment.instruction(OpCode.POP, 1)

        helper_begin(fragment, body, tc)
        fragment.instruction(OpCode.JUMP, done)
        fragment.instruction(OpCode.LABEL, next_label)

    else:
        fragment.instruction(OpCode.POP, 1)
        fragment.instruction(OpCode.CONST, None)

    fragment.instruction(OpCode.LABEL, done)
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
        fragment.instruction(OpCode.CONST, True)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr, tc if not cl else False))
        if cl:
            fragment.instruction(OpCode.JUMP_AND, done)

    fragment.instruction(OpCode.LABEL, done)
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
        fragment.instruction(OpCode.CONST, False)
        return fragment

    done = compiler.gen_label()

    while cl:
        expr, cl = cl
        fragment.position_of(expr)
        fragment.merge(compiler.full_compile(expr, tc if not cl else False))
        if cl:
            fragment.instruction(OpCode.JUMP_OR, done)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


# The end.
