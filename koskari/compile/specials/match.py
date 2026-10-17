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
from ...hygiene import extern_atoms
from ...lib import OpCode, keyword, symbol
from ...match_code import lower_clause
from ...match_core import analyse
from ...matcher import (
    BODY, CALLEDBY, FORM, MATCH, VALUE, MatchCompiler, parse_case_pattern,
    repeat_capture)
from ...syntax import special
from ...types import is_pair
from ..helpers import helper_begin


_keyword_else = keyword("else")
_keyword_when = keyword("when")
_keyword_unless = keyword("unless")

# the subject-less arm exists to carry its own diagnostic: a corpus
# record asserts the message, which a match failure would replace
_MATCH_PATTERNS = [
    MATCH(CALLEDBY("called_by")),
    MATCH(
        CALLEDBY("called_by"),
        VALUE("subject"),
        repeat_capture(
            "clauses", FORM(MATCH(VALUE("pattern"), BODY("body"))))),
]


def _lower_pattern(pattern, source, position=None):
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
        raise SpecialError(str(exc), source, position=position) from exc


@builtin("compile/match")
@special("compile/match", patterns=_MATCH_PATTERNS, unpack="match")
def special_match(compiler, caps):
    """
    Compile (compile/match SUBJECT [[PATTERN] BODY...] ... [else: BODY...]).
    Evaluates SUBJECT once, then tries each arm's pattern in order. The
    first matching arm binds its captured names and runs its body, whose
    value is the result. An [else: BODY...] arm always matches; with no
    else and no match the form yields None. Each arm's pattern rides in
    its own delimiter list ahead of the body and is classified by the
    case-pattern dialect (see docs/guide/match-patterns.md).

    A clause may carry a guard between the pattern and the body:
    [[PATTERN] when: COND BODY...] runs the body only when COND is
    truthy after the pattern matches and binds; unless: inverts the
    sense. A failed guard advances to the next arm with the subject
    intact, and the bound captures persist.
    """

    called_by = caps["called_by"]

    if "subject" not in caps:
        raise SpecialError(
            "match requires a subject", called_by.datum,
            position=called_by.position)

    fragment = compiler.fragment(called_by.datum)

    gen_label = compiler.gen_label
    done = gen_label()

    # each arm's pattern, lowered ahead of emission, and the names its
    # captures bind in order
    clauses = caps["clauses"]
    arms = []
    bound = []
    remaining = len(clauses)
    for clause in clauses:
        remaining -= 1
        patcap = clause["pattern"]
        pattern = patcap.datum
        body = clause["body"].datum
        clause_pos = patcap.position

        if pattern is _keyword_else:
            if remaining:
                raise SpecialError(
                    "else: must be last clause", pattern,
                    position=clause_pos)
            arms.append((None, None, None, None, None, body, clause_pos))
            break

        guard_kw = None
        guard_expr = None
        if is_pair(body) and (body.car is _keyword_when
                              or body.car is _keyword_unless):
            guard_kw, tail = body
            if not tail:
                raise SpecialError(
                    "match guard requires an expression", body,
                    position=clause_pos)
            guard_expr, body = tail

        arm = MATCH(_lower_pattern(pattern, pattern, clause_pos))
        tables = MatchCompiler().lower(arm)
        try:
            analysis = analyse(tables, arm=True)
        except (TypeError, ValueError) as exc:
            raise SpecialError(
                f"invalid match pattern: {exc}", pattern, position=clause_pos)
        # only the root scope's slots become variables; a name inside
        # a group or REPEAT is reached through its value
        bound.extend(analysis.scopes[0].names)
        atoms = extern_atoms(pattern, tables[1])
        arms.append(
            (pattern, tables, atoms, guard_kw, guard_expr, body, clause_pos))

    def request_externs(target):
        """
        Every extern (a user type, a predicate, a mapping key) is a
        name the pattern wrote: requested from the scope the tests
        run in, so a closure captures it, and read by the match
        instructions from the slot it lowers to. The extern table
        names each by its base spelling; the request is the atom the
        pattern wrote, so a name a template binds resolves to it
        """

        for _pattern, tables, atoms, *_rest in arms:
            if tables is None:
                continue
            for extern in tables[1]:
                target.request_var(atoms.get(extern) or symbol(extern))

    # ordered dedup: ONEOF branches and other arms may bind the same
    # name
    for name in dict.fromkeys(symbol(n) for n in bound):
        fragment.declare_var(name)

    def emit_tests(target, arm, hit, next_label):
        """
        One arm's tests against the subject on top of the stack, the
        guard after its captures bind; a pass jumps to hit with the
        subject still on the stack, a miss to next_label the same way
        """

        pattern, tables, atoms, guard_kw, guard_expr, _body, clause_pos = arm
        target.position(clause_pos)
        miss_edges = lower_clause(
            compiler, target, tables, atoms, next_label)
        if guard_kw is not None:
            target.merge(compiler.full_compile(guard_expr))
            target.instruction(
                OpCode.JUMP_FALSE if guard_kw is _keyword_when
                else OpCode.JUMP_TRUE,
                next_label)
        target.instruction(OpCode.JUMP, hit)
        miss_edges()

    # the subject stays on the stack across the arm chain; a hit pops
    # it and runs the body in place, and an arm's walk leaves nothing
    # of its own behind
    request_externs(fragment)
    fragment.merge(compiler.full_compile(caps["subject"].datum))
    for arm in arms:
        pattern, tables, atoms, guard_kw, guard_expr, body, clause_pos = arm
        if tables is None:
            fragment.position(clause_pos)
            fragment.instruction(OpCode.POP, 1)
            helper_begin(fragment, body)
            fragment.instruction(OpCode.JUMP, done)
            break
        hit = gen_label()
        next_label = gen_label()
        emit_tests(fragment, arm, hit, next_label)
        fragment.instruction(OpCode.LABEL, hit)
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
