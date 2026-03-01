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
from ...types import is_keyword, is_pair
from ...exceptions import SpecialError
from ..helpers import helper_begin, pycons, simple_parameters, special


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

        if is_keyword(test) and str(test) == "else":
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


def _expand_if_to_cond(rest, source):
    """
    Parse (compile/if COND THEN ELSE) or (compile/if COND then: THEN else: ELSE).
    Return a cond form suitable for full_compile.
    """

    pos, kw_dict = simple_parameters(rest, source)

    if kw_dict:
        cond_expr = pos[0] if pos else nil
        then_expr = kw_dict.get("then")
        else_expr = kw_dict.get("else")
        if then_expr is None or else_expr is None:
            raise SpecialError(
                "if requires both then: and else: when using keywords",
                source)
    else:
        if len(pos) < 3:
            raise SpecialError(
                "if requires condition, then, and else",
                source)

        cond_expr, then_expr, else_expr = pos[0], pos[1], pos[2]

    return pycons((symbol("compile/cond"),
                  (cond_expr, then_expr),
                  (_keyword_else, else_expr)))


def _expand_when_to_cond(rest, source):
    """
    Parse (compile/when COND BODY...). Return a cond form.
    """

    if rest is nil or not is_pair(rest):
        raise SpecialError("when requires condition and body", source)

    cond_expr, body = rest
    # body_tuple = tuple(unpack(body)) if is_pair(body) else (body,)
    return pycons((symbol("compile/cond"),
                   cons(cond_expr, body),
                   (_keyword_else, None)))


def _expand_unless_to_cond(rest, source):
    """
    Parse (compile/unless COND BODY...). Return a cond form.
    """

    if rest is nil or not is_pair(rest):
        raise SpecialError("unless requires condition and body", source)

    cond_expr, body = rest
    # body_tuple = tuple(unpack(body)) if is_pair(body) else (body,)
    return pycons((symbol("compile/cond"),
                   (cond_expr, None),
                   cons(_keyword_else, body)))


@special(symbol("compile/if"))
def special_if(compiler, source, tc=False):
    """
    Compile (compile/if COND THEN ELSE) or (compile/if COND then: THEN else: ELSE).
    Expands to cond and compiles.
    """

    called_by, rest = source
    expanded = _expand_if_to_cond(rest, source)
    return compiler.full_compile(expanded, tc)


@special(symbol("compile/when"))
def special_when(compiler, source, tc=False):
    """
    Compile (compile/when COND BODY...). Expands to cond; no else yields None.
    """

    called_by, rest = source
    expanded = _expand_when_to_cond(rest, source)
    return compiler.full_compile(expanded, tc)


@special(symbol("compile/unless"))
def special_unless(compiler, source, tc=False):
    """
    Compile (compile/unless COND BODY...). Expands to cond; negated test.
    """

    called_by, rest = source
    expanded = _expand_unless_to_cond(rest, source)
    return compiler.full_compile(expanded, tc)


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
