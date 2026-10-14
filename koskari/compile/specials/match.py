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
from ...lib import OpCode, gensym, keyword, symbol
from ...matcher import (
    BODY, CALLEDBY, FORM, MATCH, VALUE, MatchCompiler, parse_case_pattern,
    repeat_capture)
from ...syntax import special
from ...types import is_pair
from ..helpers import helper_begin
from ...match_code import lower_clause


# The layout that keeps a form's cursor, mark and group temporaries in
# a namespace of the form's own, delivering the matched arm's index to
# the enclosing scope. Off: every capture the form binds is a
# write-through closure of that namespace, made on each entry, and the
# churn match_case kernel went from 0.21 s to 0.97 s with it on
# (2026-10-14). With it off the temporaries are gensym locals of the
# enclosing scope.
SCOPED_TEMPORARIES = False

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

    # each arm's pattern, lowered ahead of emission: the frozen tuples
    # decide the layout, since an arm whose pattern opens a cursor
    # needs temporaries and a form with any such arm keeps them in a
    # namespace of its own
    clauses = caps["clauses"]
    arms = []
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
        frozen = MatchCompiler().lower(arm)
        atoms = extern_atoms(pattern, frozen[2])
        arms.append(
            (pattern, frozen, atoms, guard_kw, guard_expr, body, clause_pos))

    def request_externs(target):
        """
        Every extern (a user type, a predicate, a mapping key) is a
        name the pattern wrote: requested from the scope the tests
        run in, so a closure captures it, and read by the match
        instructions from the slot it lowers to. The extern table
        names each by its base spelling; the request is the atom the
        pattern wrote, so a name a template binds resolves to it
        """

        for _pattern, frozen, atoms, *_rest in arms:
            if frozen is None:
                continue
            for extern in frozen[2]:
                target.request_var(atoms.get(extern) or symbol(extern))

    declared = set()
    for pattern, frozen, atoms, _kw, _expr, _body, _pos in arms:
        if frozen is None:
            continue
        # frozen[1] is the full slot table in emission order -- the
        # one lowering yields both the instructions and the binding
        # names. Ordered dedup: ONEOF branches may declare the same
        # slot in mutually exclusive clauses.
        for name in dict.fromkeys(symbol(n) for n in frozen[1]):
            if name not in declared:
                declared.add(name)
                fragment.declare_var(name)

    scoped = SCOPED_TEMPORARIES and any(
        frozen is not None and any(node[0] == 'FORM' for node in frozen[4])
        for _p, frozen, *_rest in arms)

    def emit_tests(target, index, arm, hit, next_label):
        """
        One arm's tests against the subject on top of the stack, the
        guard after its captures bind; a pass jumps to hit with the
        subject still on the stack, a miss to next_label the same way
        """

        pattern, frozen, atoms, guard_kw, guard_expr, _body, clause_pos = arm
        target.position(clause_pos)
        miss_edges = lower_clause(
            compiler, target, frozen, atoms, next_label, [],
            pattern, clause_pos)
        if guard_kw is not None:
            target.merge(compiler.full_compile(guard_expr))
            target.instruction(
                OpCode.JUMP_FALSE if guard_kw is _keyword_when
                else OpCode.JUMP_TRUE,
                next_label)
        target.instruction(OpCode.JUMP, hit)
        miss_edges()

    if not scoped:
        # no arm opens a cursor: the subject stays on the stack across
        # the arm chain, a hit pops it and runs the body in place
        request_externs(fragment)
        fragment.merge(compiler.full_compile(caps["subject"].datum))
        for arm in arms:
            pattern, frozen, atoms, guard_kw, guard_expr, body, clause_pos = arm
            if frozen is None:
                fragment.position(clause_pos)
                fragment.instruction(OpCode.POP, 1)
                helper_begin(fragment, body)
                fragment.instruction(OpCode.JUMP, done)
                break
            hit = gen_label()
            next_label = gen_label()
            emit_tests(fragment, None, arm, hit, next_label)
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

    # an arm opens a cursor: the subject is bound into a namespace of
    # the form's own, where every cursor, mark and group local lives,
    # each arm reads it back and tests it there, and the namespace
    # delivers the index of the arm that matched, or None, to the
    # enclosing scope, which dispatches to the arm's body outside it.
    # The captures are the enclosing scope's names and bind there.
    subject_name = gensym("match-subject")
    with fragment.child_namespace(
            called_by.datum, (subject_name,), (caps["subject"].datum,)) as child:
        # the externs are read and the captures written from inside
        # the namespace; the captures are the enclosing scope's names
        request_externs(child)
        for name in declared:
            child.request_for_write(name)
        out = gen_label()
        for index, arm in enumerate(arms):
            pattern, frozen, atoms, guard_kw, guard_expr, body, clause_pos = arm
            if frozen is None:
                child.position(clause_pos)
                child.instruction(OpCode.CONST, index)
                child.instruction(OpCode.JUMP, out)
                break
            hit = gen_label()
            next_label = gen_label()
            child.instruction(OpCode.VAR, subject_name)
            emit_tests(child, index, arm, hit, next_label)
            child.instruction(OpCode.LABEL, hit)
            child.instruction(OpCode.POP, 1)
            child.instruction(OpCode.CONST, index)
            child.instruction(OpCode.JUMP, out)
            child.instruction(OpCode.LABEL, next_label)
            child.instruction(OpCode.POP, 1)
        else:
            child.instruction(OpCode.CONST, None)
        child.instruction(OpCode.LABEL, out)

    labels = [gen_label() for _arm in arms]
    for index, label in enumerate(labels):
        fragment.instruction(OpCode.SWITCH_EQ_CONST, (label, index, 0, 0))
    fragment.instruction(OpCode.POP, 1)
    fragment.instruction(OpCode.CONST, None)
    fragment.instruction(OpCode.JUMP, done)
    for label, arm in zip(labels, arms):
        body, clause_pos = arm[5], arm[6]
        fragment.instruction(OpCode.LABEL, label)
        fragment.position(clause_pos)
        helper_begin(fragment, body)
        fragment.instruction(OpCode.JUMP, done)

    fragment.instruction(OpCode.LABEL, done)
    return fragment


# The end.
