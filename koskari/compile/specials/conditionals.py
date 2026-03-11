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
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import cons, keyword, nil, OpCode, symbol
from ...exceptions import SpecialError
from ..helpers import helper_begin, special, special_forms
from ...matcher import MATCH, EXPR, VALUE, LITERAL_KW, match


_keyword_else = keyword("else")


@special(symbol("compile/cond"))
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


# _IF_FORMS = [
#     MATCH(VALUE("test"), EXPR("then"), EXPR("else")),
#     MATCH(VALUE("test"),
#           LITERAL_KW("then"), EXPR("then"),
#           LITERAL_KW("else"), EXPR("else")),
# ]


# @special_forms(_IF_FORMS)
# @special(symbol("compile/if"))
# def special_if(compiler, source, tc=False):
#     """
#     Compile (compile/if COND THEN ELSE) or (compile/if COND then: THEN else: ELSE).
#     Expands to cond and compiles.
#     """

#     called_by, rest = source

#     m = match(rest, _IF_FORMS)
#     if m is None:
#         raise SpecialError("invalid if form", source)

#     test_src = m["test"].source
#     then_src = m["then"].source
#     else_src = m["else"].source

#     return cons(symbol("compile/cond"),
#                cons(test_src, then_src, nil),
#                cons(_keyword_else, else_src, nil),
#                nil)


# _WHEN_FORMS = [
#     MATCH(VALUE("test"), EXPR("body")),
# ]

# @special_forms(_WHEN_FORMS)
# @special(symbol("compile/when"))
# def special_when(compiler, source, tc=False):
#     """
#     Compile (compile/when COND BODY...). Expands to cond; no else yields None.
#     """

#     called_by, rest = source

#     m = match(rest, _WHEN_FORMS)
#     if m is None:
#         raise SpecialError("invalid if form", source)

#     test_src = m["test"].source
#     body_src = m["body"].source

#     return cons(symbol("compile/cond"),
#                cons(test_src, body_src, nil),
#                cons(_keyword_else, None, nil),
#                nil)


# @special_forms(_WHEN_FORMS)
# @special(symbol("compile/unless"))
# def special_unless(compiler, source, tc=False):
#     """
#     Compile (compile/unless COND BODY...). Expands to cond; negated test.
#     """

#     called_by, rest = source

#     m = match(rest, _WHEN_FORMS)
#     if m is None:
#         raise SpecialError("invalid if form", source)

#     test_src = m["test"].source
#     body_src = m["body"].source

#     return cons(symbol("compile/cond"),
#                cons(test_src, None, nil),
#                cons(_keyword_else, body_src, nil),
#                nil)


@special(symbol("compile/and"))
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


@special(symbol("compile/or"))
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
