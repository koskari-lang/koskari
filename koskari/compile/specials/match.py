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
koskari.compile.specials.match

The compile/match special: a subject matched against case patterns
lowered through the runtime case-pattern dialect, with a clause body
compiled per pattern and an optional else clause.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import Matcher, OpCode, keyword
from ...matcher import CALLEDBY, MATCH, REST, MatchCompiler, parse_case_pattern
from ...syntax import special
from ...types import is_pair
from ..helpers import helper_begin


_keyword_else = keyword("else")
_keyword_when = keyword("when")
_keyword_unless = keyword("unless")

_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


def _lower_pattern(pattern, source):
    """
    Lower one clause delimiter's content through the runtime
    case-pattern dialect. parse_case_pattern classifies by head symbol
    or marker (#list / #tuple sequences, item class sugar, ::-typed,
    combinators, atoms) -- the special makes no shape decisions of its
    own.
    """

    try:
        return parse_case_pattern(pattern)
    except ValueError as exc:
        raise SpecialError(str(exc), source) from exc


@builtin("compile/match")
@special("compile/match", patterns=_DEFAULT_PATTERNS)
def special_match(compiler, source):
    """
    Compile (compile/match SUBJECT [[PATTERN] BODY...] ... [else: BODY...]).
    Evaluates SUBJECT once, then tries each arm's pattern in order. The
    first matching arm binds its captured names and runs its body, whose
    value is the result. An [else: BODY...] arm always matches; with no
    else and no match the form yields None. Each arm's pattern rides in
    its own delimiter list ahead of the body and is classified by the
    case-pattern dialect (see design/language/match-pattern-language.md).

    A clause may carry a guard between the pattern and the body:
    [[PATTERN] when: COND BODY...] runs the body only when COND is
    truthy after the pattern matches and binds; unless: inverts the
    sense. A failed guard advances to the next arm with the subject
    intact, and the bound captures persist.
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
            helper_begin(fragment, body)
            fragment.instruction(OpCode.JUMP, done)
            break

        guard_kw = None
        guard_expr = None
        if is_pair(body) and (body.car is _keyword_when
                              or body.car is _keyword_unless):
            guard_kw, tail = body
            if not tail:
                raise SpecialError(
                    "match guard requires an expression", source)
            guard_expr, body = tail

        arm = MATCH(_lower_pattern(pattern, source))
        frozen = MatchCompiler().lower(arm)
        # frozen[1] is the full slot table in emission order -- the one
        # lowering yields both the constant and the binding names.
        # Ordered dedup: ONEOF branches may declare the same slot in
        # mutually exclusive arms.
        names = list(dict.fromkeys(str(n) for n in frozen[1]))

        # externs covered by the matcher extern defaults (int, str,
        # pair, ...) resolve from that baked-in table at match time and
        # never touch the namespace; only the missing ones are loaded,
        # as ordinary variable reads pushed above the subject in
        # extern-table order for MATCH_CASE to consume. The loads lower
        # to FAST with every other read, and the request also captures
        # a closure-bound type used only in a pattern.
        matcher = Matcher.from_tuple(frozen)
        externs = [str(name) for name
                   in matcher.missing_externs(exclude_defaults=True)]
        for extern in externs:
            fragment.request_var(extern)
            fragment.instruction(OpCode.VAR, extern)

        # the opcode binds the arm's captures itself, resolving the
        # slot names to namespace slots once per site through its
        # cache; the names are declared here so they get slots
        next_label = gen_label()
        fragment.instruction(
            OpCode.MATCH_CASE,
            (next_label, frozen, tuple(externs),
             1 if guard_kw is not None else 0))

        for name in names:
            if name not in declared:
                declared.add(name)
                fragment.declare_var(name)

        if guard_kw is not None:
            # the subject is still on the stack (keep flag); a failed
            # guard advances with it intact, a passed guard pops it and
            # commits to the body
            fragment.merge(compiler.full_compile(guard_expr))
            fragment.instruction(
                OpCode.JUMP_FALSE if guard_kw is _keyword_when
                else OpCode.JUMP_TRUE,
                next_label)
            fragment.instruction(OpCode.POP, 1)

        helper_begin(fragment, body)
        fragment.instruction(OpCode.JUMP, done)
        fragment.instruction(OpCode.LABEL, next_label)

    else:
        fragment.instruction(OpCode.POP, 1)
        fragment.instruction(OpCode.CONST, None)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


# The end.
