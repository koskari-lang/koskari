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
Tests for the koskari.compile match special form: delimiter clauses
([[PATTERN] BODY...]) classified by the runtime case-pattern dialect

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from dataclasses import dataclass

import asyncio

import pytest

from koskari.exceptions import SpecialError
from koskari.lib import Datum, cons, nil, setcdr

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


@dataclass
class Point:
    x: int
    y: int


class TestMatchSpecial(SpecialsTestCase):

    async def test_match_no_clauses(self):
        expr, mod = self.compile_expr("(match 5)")
        self.assertEqual(await expr(), None)


    async def test_match_else_only(self):
        expr, mod = self.compile_expr("(match 5 [else: 100])")
        self.assertEqual(await expr(), 100)


    async def test_match_literal(self):
        src = """
        (match data
          [[1] "one"]
          [[5] "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src, data=5)
        self.assertEqual(await expr(), "five")


    async def test_match_literal_no_match_to_else(self):
        src = """
        (match data
          [[1] "one"]
          [[5] "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src, data=9)
        self.assertEqual(await expr(), "meh")


    async def test_match_capture_whole_value(self):
        src = """
        (match data
          [[x] x])
        """
        expr, mod = self.compile_expr(src, data=42)
        self.assertEqual(await expr(), 42)


    async def test_match_typed_multi_token(self):
        src = """
        (match data
          [[x :: int] x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data=7, int=int)
        self.assertEqual(await expr(), 7)


    async def test_match_typed_multi_token_no_match(self):
        src = """
        (match data
          [[x :: int] x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data="hello", int=int)
        self.assertEqual(await expr(), "nope")


    async def test_match_typed_single_form(self):
        src = """
        (match data
          [[(x :: int)] x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data=7, int=int)
        self.assertEqual(await expr(), 7)


    async def test_match_sequence(self):
        src = """
        (match data
          [[#[a b c]] b]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 2)


    async def test_match_sequence_wrong_length_falls_through(self):
        src = """
        (match data
          [[#[a b]] b]
          [else: 99])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 99)


    async def test_match_string_is_not_a_sequence(self):
        # PEP 634: a string never destructures under a sequence pattern
        src = """
        (match data
          [[#[a b *: rest]] "seq"]
          [[s :: str] "string"]
          [else: "other"])
        """
        expr, mod = self.compile_expr(src, data="plain", str=str)
        self.assertEqual(await expr(), "string")


    async def test_match_sequence_rest(self):
        src = """
        (match data
          [[#[a *: rest]] rest]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), (2, 3))


    async def test_match_sequence_empty_rest_before_suffix(self):
        # a mid-spine rest that consumes nothing binds the empty
        # remainder, and the suffix still binds the last element
        src = """
        (match data
          [[#[a *: mid b]] #(a mid b)]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2))
        self.assertEqual(await expr(), (1, (), 2))

        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), (1, (2,), 3))


    async def test_match_sequence_over_pair_binds_datum(self):
        # a pair is source: walking it boxes each capture in a Datum
        # carrying the cell, so the bound name is the box
        src = """
        (match data
          [[#[a b c]] b]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=cons(1, 2, 3, nil))
        result = await expr()
        self.assertIsInstance(result, Datum)
        self.assertEqual(result.datum, 2)


    async def test_match_sequence_rest_over_pair_binds_datum(self):
        src = """
        (match data
          [[#[a *: rest]] rest]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=cons(1, 2, 3, nil))
        result = await expr()
        self.assertIsInstance(result, Datum)
        self.assertEqual(result.datum, cons(2, 3, nil))


    async def test_match_class(self):
        src = """
        (match data
          [[(item Point a b)] b]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(
            src, data=Point(3, 4), Point=Point)
        self.assertEqual(await expr(), 4)


    async def test_match_class_no_match(self):
        src = """
        (match data
          [[(item Point a b)] b]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data="zzz", Point=Point)
        self.assertEqual(await expr(), "nope")


    async def test_match_or(self):
        src = """
        (match data
          [[(or 1 2)] "low"]
          [else: "high"])
        """
        expr, mod = self.compile_expr(src, data=2)
        self.assertEqual(await expr(), "low")


    async def test_match_not(self):
        src = """
        (match data
          [[(not 9)] "not-nine"]
          [else: "nine"])
        """
        expr, mod = self.compile_expr(src, data=5)
        self.assertEqual(await expr(), "not-nine")
        expr, mod = self.compile_expr(src, data=9)
        self.assertEqual(await expr(), "nine")


    async def test_match_not_in_sequence(self):
        # the case dialect's (not p) is a whole element: present, and
        # not matching p
        src = """
        (match data
          [[#[(not 9) x]] x]
          [else: "nine"])
        """
        expr, mod = self.compile_expr(src, data=(5, 7))
        self.assertEqual(await expr(), 7)
        expr, mod = self.compile_expr(src, data=(9, 7))
        self.assertEqual(await expr(), "nine")
        expr, mod = self.compile_expr(src, data=(5,))
        self.assertEqual(await expr(), "nine")


    async def test_match_or_shared_slot_binds(self):
        # ONEOF branches may declare the same slot; the arm's binding
        # step dedups the name and binds whichever branch matched
        src = """
        (match data
          [[(or (s :: str) (s :: int))] s]
          [else: "no"])
        """
        expr, mod = self.compile_expr(src, data=7, int=int, str=str)
        self.assertEqual(await expr(), 7)
        expr, mod = self.compile_expr(src, data="hi", int=int, str=str)
        self.assertEqual(await expr(), "hi")



    async def test_match_or_shares_a_name_with_a_repeat(self):
        # a name bound inside a REPEAT in one alternative and directly
        # in another: the REPEAT binds the list of its per-pass values
        src = """
        (match data
          [[#[(or (REPEAT (MATCH (BIND "x") 2)) (MATCH (BIND "x") 1))]]
           x]
          [else: "none"])
        """
        expr, mod = self.compile_expr(src, data=(5, 2, 6, 2))
        self.assertEqual(await expr(), [5, 6])
        expr, mod = self.compile_expr(src, data=(5, 3))
        self.assertEqual(await expr(), "none")


    async def test_match_predicate(self):
        src = """
        (match data
          [[(? even)] "even"]
          [else: "odd"])
        """
        even = lambda v: v % 2 == 0  # noqa: E731
        expr, mod = self.compile_expr(src, data=4, even=even)
        self.assertEqual(await expr(), "even")
        expr, mod = self.compile_expr(src, data=5, even=even)
        self.assertEqual(await expr(), "odd")


    async def test_match_mapping(self):
        src = """
        (match data
          [[#{"kind": "job" "url": u}] u]
          [[#{"kind": k}] k]
          [else: "other"])
        """
        expr, mod = self.compile_expr(
            src, data={"kind": "job", "url": "http://x"})
        self.assertEqual(await expr(), "http://x")
        expr, mod = self.compile_expr(src, data={"kind": "task"})
        self.assertEqual(await expr(), "task")
        expr, mod = self.compile_expr(src, data={"nope": 1})
        self.assertEqual(await expr(), "other")


    async def test_match_mapping_probes_without_inserting(self):
        """
        a mapping pattern reads the subject without changing it: a
        defaultdict gains no key from a missed arm, and a key it does
        hold still matches
        """

        from collections import defaultdict
        src = """
        (match data
          [[#{"kind": k}] k]
          [else: "other"])
        """
        data = defaultdict(lambda: "invented")
        expr, mod = self.compile_expr(src, data=data)
        self.assertEqual(await expr(), "other")
        self.assertEqual(dict(data), {})

        data["kind"] = "job"
        expr, mod = self.compile_expr(src, data=data)
        self.assertEqual(await expr(), "job")


    async def test_match_mapping_extern_key(self):
        src = """
        (match data
          [[#{field: v}] v]
          [else: "missing"])
        """
        expr, mod = self.compile_expr(
            src, data={"dyn": 42}, field="dyn")
        self.assertEqual(await expr(), 42)
        expr, mod = self.compile_expr(
            src, data={"dyn": 42}, field="absent")
        self.assertEqual(await expr(), "missing")


    async def test_match_app_extraction(self):
        src = """
        (match data
          [[(app divmod3 #[q 0])] q]
          [[(app divmod3 #[q r])] r]
          [else: "meh"])
        """
        dm3 = lambda v: divmod(v, 3)  # noqa: E731
        expr, mod = self.compile_expr(src, data=9, divmod3=dm3)
        self.assertEqual(await expr(), 3)
        expr, mod = self.compile_expr(src, data=10, divmod3=dm3)
        self.assertEqual(await expr(), 1)


    async def test_match_first_arm_wins(self):
        src = """
        (match data
          [[#[a b]] "two"]
          [[#[a b c]] "three"]
          [else: "other"])
        """
        expr, mod = self.compile_expr(src, data=(1, 2))
        self.assertEqual(await expr(), "two")


    async def test_match_not_after_the_element_tests_the_next(self):
        # the root is a single value walked as a one-element subject:
        # the NOT after x tests the end, as it does in a sequence and
        # in an or branch
        src = """
        (match data
          [[(MATCH x (NOT 1))] #("root" x)]
          [else: "else"])
        """
        for data in (1, 2):
            expr, mod = self.compile_expr(src, data=data)
            self.assertEqual(await expr(), ("root", data))

        src = """
        (match data
          [[#[(MATCH x (NOT 1))]] #("plain" x)]
          [else: "else"])
        """
        expr, mod = self.compile_expr(src, data=(1,))
        self.assertEqual(await expr(), ("plain", 1))

        src = """
        (match data
          [[#[(or (MATCH x (NOT 1)) 9)]] #("or" x)]
          [else: "else"])
        """
        expr, mod = self.compile_expr(src, data=(1,))
        self.assertEqual(await expr(), ("or", 1))


    async def test_match_arm_roots_of_each_kind(self):
        # a root of any kind whose width range includes 1 walks the
        # subject as a one-element sequence
        for root, expected in (
                ('(GROUP g (BIND "a"))', {"a": 7}),
                ('(REPEAT g (BIND "a") min: 0)', [{"a": 7}]),
                ('(OPTIONAL (BIND "g"))', 7),
                ("(REST g)", (7,))):
            src = f"""
            (match data
              [[{root}] g]
              [else: "else"])
            """
            expr, mod = self.compile_expr(src, data=7)
            self.assertEqual(await expr(), expected, root)


    async def test_match_not_beside_an_or_with_a_not(self):
        src = """
        (match data
          [[#[#[(not 2)] (or _ (not 9))]] "hit"]
          [else: "other"])
        """
        for data, expected in (
                (((1,), 5), "hit"),
                (((1,), 9), "hit"),
                (((2,), 5), "other"),
                (((1,),), "other"),
                (((), 5), "other")):
            expr, mod = self.compile_expr(src, data=data)
            self.assertEqual(await expr(), expected, data)


    async def test_match_repeat_slot_binds_the_per_pass_values(self):
        src = """
        (match data
          [[#[(REPEAT a) z]] #(a z)]
          [else: "else"])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), ([1, 2], 3))
        expr, mod = self.compile_expr(src, data=(3,))
        self.assertEqual(await expr(), ([], 3))


    async def test_match_bare_multi_token_raises(self):
        src = """
        (match data
          [[a b c] "seq?"]
          [else: 0])
        """
        with self.assertRaises(SpecialError):
            self.compile_expr(src, data=(1, 2, 3))


class TestMatchFailedCaptures(SpecialsTestCase):
    """
    A missed arm writes no variable, and a hit writes only the slots
    it filled: a binding of a capture name made before the match keeps
    its value through a miss and through a hit that leaves the name's
    slot empty
    """

    async def test_plain_miss_writes_no_variable(self):
        src = """
        (let [[x 100]]
          (match data
            [[#[x 1]] x]
            [else: x]))
        """
        expr, mod = self.compile_expr(src, data=(5, 3))
        self.assertEqual(await expr(), 100)


    async def test_failed_alternative_leaves_the_variable_as_it_was(self):
        src = """
        (let [[x 100]]
          (match data
            [[(or #[x 1] #[5 2])] x]
            [else: x]))
        """
        expr, mod = self.compile_expr(src, data=(5, 2))
        self.assertEqual(await expr(), 100)
        expr, mod = self.compile_expr(src, data=(5, 3))
        self.assertEqual(await expr(), 100)


    async def test_unfired_optional_leaves_the_variable_as_it_was(self):
        src = """
        (let [[b "default"]]
          (match data
            [[#[a (OPTIONAL (BIND "b"))]] #(a b)]
            [else: #("else" b)]))
        """
        expr, mod = self.compile_expr(src, data=(1,))
        self.assertEqual(await expr(), (1, "default"))
        expr, mod = self.compile_expr(src, data=(1, 2))
        self.assertEqual(await expr(), (1, 2))
        expr, mod = self.compile_expr(src, data="x")
        self.assertEqual(await expr(), ("else", "default"))


    async def test_unfired_optional_leaves_its_name_unbound(self):
        src = """
        (match data
          [[#[x (OPTIONAL y)]] y]
          [else: "no"])
        """
        expr, mod = self.compile_expr(src, data=(5, 1))
        self.assertEqual(await expr(), 1)
        expr, mod = self.compile_expr(src, data=(5,))
        with self.assertRaises(NameError):
            await expr()


class TestMatchGuards(SpecialsTestCase):
    """
    when: / unless: guards run after the pattern binds; a failed guard
    advances to the next arm with the subject intact.
    """

    async def test_when_guard_passes(self):
        src = """
        (match data
          [[x :: int] when: (big x) "big"]
          [else: "other"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=50, int=int, big=big)
        self.assertEqual(await expr(), "big")


    async def test_when_guard_fails_advances(self):
        src = """
        (match data
          [[x :: int] when: (big x) "big"]
          [[y :: int] "small"]
          [else: "other"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=5, int=int, big=big)
        self.assertEqual(await expr(), "small")


    async def test_unless_guard(self):
        src = """
        (match data
          [[x :: int] unless: (big x) "small"]
          [else: "other"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=5, int=int, big=big)
        self.assertEqual(await expr(), "small")
        expr, mod = self.compile_expr(src, data=50, int=int, big=big)
        self.assertEqual(await expr(), "other")


    async def test_guard_sees_bound_captures(self):
        src = """
        (match data
          [[#[a b]] when: (bigger a b) a]
          [[#[a b]] b]
          [else: 0])
        """
        bigger = lambda x, y: x > y  # noqa: E731
        expr, mod = self.compile_expr(src, data=(9, 2), bigger=bigger)
        self.assertEqual(await expr(), 9)
        expr, mod = self.compile_expr(src, data=(2, 9), bigger=bigger)
        self.assertEqual(await expr(), 9)


    async def test_guard_fail_then_else(self):
        src = """
        (match data
          [[x] when: (big x) "big"]
          [else: "meh"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=5, big=big)
        self.assertEqual(await expr(), "meh")


    async def test_guard_missing_expression_raises(self):
        src = """
        (match data
          [[x] when:])
        """
        with self.assertRaises(SpecialError):
            self.compile_expr(src, data=5)


class TestMatchLayout(SpecialsTestCase):
    """
    End-to-end tests through the match/case block layout, which wraps
    each case header into the clause delimiter for match.
    """

    async def test_layout_sequence(self):
        src = """
        match data:
          case #[a b c]:
            b
          else:
            0
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 2)


    async def test_layout_literal(self):
        src = """
        match data:
          case 1:
            "one"
          case 5:
            "five"
          else:
            "meh"
        """
        expr, mod = self.compile_expr(src, data=5)
        self.assertEqual(await expr(), "five")


    async def test_layout_capture_whole_value(self):
        src = """
        match data:
          case x:
            x
        """
        expr, mod = self.compile_expr(src, data=42)
        self.assertEqual(await expr(), 42)


    async def test_layout_typed_multi_token(self):
        src = """
        match data:
          case x :: int:
            x
          else:
            "nope"
        """
        expr, mod = self.compile_expr(src, data=7, int=int)
        self.assertEqual(await expr(), 7)


    async def test_layout_class(self):
        src = """
        match data:
          case (item Point a b):
            b
          else:
            "nope"
        """
        expr, mod = self.compile_expr(
            src, data=Point(3, 4), Point=Point)
        self.assertEqual(await expr(), 4)


    async def test_layout_rest_and_no_else(self):
        src = """
        match data:
          case #[a *: rest]:
            a
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 1)


    async def test_layout_no_match_no_else_is_none(self):
        src = """
        match data:
          case #[a b]:
            b
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), None)


    async def test_layout_when_guard(self):
        src = """
        match data:
          case x :: int when (big x):
            "big"
          case y :: int:
            "small"
          else:
            "other"
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=50, int=int, big=big)
        self.assertEqual(await expr(), "big")
        expr, mod = self.compile_expr(src, data=5, int=int, big=big)
        self.assertEqual(await expr(), "small")


    async def test_layout_unless_guard(self):
        src = """
        match data:
          case x unless (big x):
            "small"
          else:
            "big"
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=5, big=big)
        self.assertEqual(await expr(), "small")
        expr, mod = self.compile_expr(src, data=50, big=big)
        self.assertEqual(await expr(), "big")


class TestMatchExternResolution(SpecialsTestCase):
    """
    A pattern's type / class externs are resolved from the namespace at
    match time, so the special must request each one through resolution.
    Otherwise a name bound in an enclosing scope and used only in a
    pattern is never captured into the closure.
    """

    async def test_pattern_extern_is_closure_captured(self):
        # T is bound by the outer let and referenced only inside the
        # match pattern of the returned lambda, so it can only resolve if
        # the extern name was requested (and thus captured).
        src = """
        (let [[T int]]
          (lambda [data]
            (match data
              [[x :: T] "isT"]
              [else: "no"])))
        """
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertEqual(await fn(7), "isT")
        self.assertEqual(await fn("hi"), "no")


    async def test_core_type_name_is_not_a_module_name(self):
        # int in a pattern is the core type test; a module binding of
        # int is neither read nor requested
        src = """
        (match data
          [[x :: int] "int"]
          [else: "no"])
        """
        expr, mod = self.compile_expr(src, data=7, int="not a type")
        self.assertEqual(await expr(), "int")
        expr, mod = self.compile_expr(src, data="s", int="not a type")
        self.assertEqual(await expr(), "no")


    async def test_macro_pattern_and_user_pattern_both_naming_int(self):
        # a core type name goes through no hygiene table, so the
        # macro's own (a :: int) and the user's (b :: int) coexist
        src_str = """
        (define m
          (syntax-rules m []
            [(m v p) (match v [[#[(a :: int) p]] (+ a b)] [else: 0])]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/m': vars(mod)['m']}
        expr, mod = self.compile_expr("(m (#list 1 2) (b :: int))", **defn)
        self.assertEqual(3, await expr())
        expr, mod = self.compile_expr('(m (#list 1 "x") (b :: int))', **defn)
        self.assertEqual(0, await expr())


    async def test_looping_spine_in_the_subject_is_refused(self):
        # the descent into the subject refuses a looping spine for any
        # arm shape, as the engine refused it before any arm ran
        cyclic = cons(1, 2, nil)
        setcdr(cyclic.cdr, cyclic)
        src = """
        (match data
          [[#[a b]] "two"]
          [else: "no"])
        """
        expr, mod = self.compile_expr(src, data=cyclic)
        with self.assertRaises(ValueError):
            await expr()
        expr, mod = self.compile_expr(src, data=cons(1, 2, nil))
        self.assertEqual(await expr(), "two")


    async def test_spine_looped_behind_the_cursor_is_refused(self):
        # a predicate closes a loop behind the cursor's cell while a
        # repeat walks the spine: the pass's span never reaches the
        # cursor, and the iteration raises instead of walking forever
        subject = cons(1, 2, 3, nil)

        def loop(v):
            if v.datum == 2:
                setcdr(subject.cdr, subject)
            return True

        src = """
        (match data
          [[#[(REPEAT xs (PREDICATE loop) min: 0) z]] "hit"]
          [else: "miss"])
        """
        expr, mod = self.compile_expr(src, data=subject, loop=loop)
        with self.assertRaises(ValueError) as caught:
            await expr()
        self.assertIn("changed under the walk", str(caught.exception))




class TestMatchTemporaries(SpecialsTestCase):
    """
    The captures and the body's own definitions of an arm land in the
    enclosing scope, whatever temporaries the arm's walk declares
    """

    async def test_function_scope_captures_and_definitions(self):
        src = """
        (lambda [data]
          (match data
            [[#[a b *: rest]] when: (< a b) (define kind "ascending")]
            [[#[a b *: rest]] (define kind "other")]
            [[x] (define kind "one")]
            [else: (define kind "none")])
          #(kind a b rest))
        """
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn((1, 2, 3)), ("ascending", 1, 2, (3,)))
        self.assertEqual(await fn((2, 1)), ("other", 2, 1, ()))


    def test_arms_declare_no_temporaries(self):
        """
        An arm's cursors and marks live on the operand stack for the
        walk's extent and leave no local behind
        """

        src = """
        (lambda [data]
          (match data
            [[#[a 2]] a]
            [[#[b c]] (+ b c)]
            [[#[d *: more]] d]
            [else: "none"]))
        """
        code = self.compile_to_code(src)
        inner = next(
            c.code if hasattr(c, "code") else c
            for c in code.constants_pool
            if hasattr(c, "soften") or hasattr(c, "code"))
        names = [str(n) for n in inner.namespace_spec.local_syms]
        self.assertEqual(
            sorted(names), ["a", "b", "c", "d", "data", "more"])




class TestPatternValidation(SpecialsTestCase):
    """
    A pattern the validator refuses is refused when the form compiles,
    naming the clause, not when an arm runs
    """

    def test_capture_under_not_is_refused(self):
        with self.assertRaises(SpecialError) as caught:
            self.compile_expr("(match data [[(not x)] 1] [else: 0])", data=1)
        self.assertIn("NOT", str(caught.exception))


    def test_optional_after_rest_is_refused(self):
        with self.assertRaises(SpecialError) as caught:
            self.compile_expr(
                "(match data [[#[*: r (OPTIONAL a)]] 1] [else: 0])", data=(1,))
        self.assertIn("REST", str(caught.exception))


    def assertRefused(self, pattern):
        src = f"(match data [[{pattern}] 1] [else: 0])"
        with self.assertRaises(SpecialError, msg=pattern):
            self.compile_expr(src, data=1, f=abs, P=int)


    def test_root_width_without_one_is_refused(self):
        # an arm's subject is a single value; the case dialect writes
        # a NOT that consumes the value as (not p)
        for pattern in ("(NOT 1)", "(MATCH a b)", "(MATCH)"):
            self.assertRefused(pattern)


    def test_single_value_width_without_one_is_refused(self):
        for pattern in ("(app f (MATCH a b))", "(app f (NOT 1))",
                        '#{"k": (MATCH a b)}', '#{"k": (NOT 1)}'):
            self.assertRefused(pattern)


    def test_field_width_not_one_is_refused(self):
        for pattern in ("(item P (REST r))", "(item P (MATCH a b))",
                        "(item P (OPTIONAL a))"):
            self.assertRefused(pattern)


    def test_pair_literal_is_refused(self):
        for pattern in ("(LITERAL (1 2))", "#[a (LITERAL (1 2))]"):
            self.assertRefused(pattern)


    def test_repeat_body_of_minimum_width_zero_is_refused(self):
        for pattern in ("#[(REPEAT xs (OPTIONAL a) min: 0)]",
                        "#[(REPEAT xs (NOT 1) min: 0)]"):
            self.assertRefused(pattern)


    def test_variable_width_after_rest_is_refused(self):
        for pattern in ("#[*: r (REPEAT xs a min: 0)]",
                        "#[*: r (REST s)]",
                        "#[*: r (or a (MATCH b c))]"):
            self.assertRefused(pattern)


    def test_capture_anywhere_under_not_is_refused(self):
        for pattern in ("#[(not #[a]) b]", "#[(NOT (MATCH a)) b]",
                        "#[(not (app f a)) b]"):
            self.assertRefused(pattern)


    def test_name_bound_twice_in_one_scope_is_refused(self):
        for pattern in ("#[x x]", "#[(or x 1) x]"):
            self.assertRefused(pattern)


    def test_count_past_the_field_limit_is_refused(self):
        for pattern in ("#[(REPEAT xs a min: 70000)]",
                        "#[(REST r min: 70000)]"):
            self.assertRefused(pattern)


class TestMatchCalls(SpecialsTestCase):
    """
    A predicate or extractor the pattern calls runs in the matcher's
    frame: a Koskari one flattens, so a continuation captured inside it
    crosses the match, and an awaiting one awaits in place with the
    walk resuming where it stopped
    """

    async def test_awaiting_predicate_awaits_in_place(self):
        src = """
        (match data
          [[(? even)] "even"]
          [else: "odd"])
        """

        async def even(v):
            await asyncio.sleep(0)
            return v % 2 == 0

        expr, mod = self.compile_expr(src, data=4, even=even)
        self.assertEqual(await expr(), "even")
        expr, mod = self.compile_expr(src, data=5, even=even)
        self.assertEqual(await expr(), "odd")


    async def test_suspension_mid_walk_resumes_the_walk(self):
        src = """
        (match data
          [[#[(? slow) y]] y]
          [else: "miss"])
        """

        async def slow(v):
            await asyncio.sleep(0)
            # a pair spine hands the predicate the boxed element
            return (v.datum if isinstance(v, Datum) else v) > 3

        expr, mod = self.compile_expr(src, data=(4, 9), slow=slow)
        self.assertEqual(await expr(), 9)
        expr, mod = self.compile_expr(src, data=(1, 9), slow=slow)
        self.assertEqual(await expr(), "miss")
        expr, mod = self.compile_expr(src, data=cons(4, 9, nil), slow=slow)
        self.assertEqual((await expr()).datum, 9)


    async def test_koskari_predicate_flattens(self):
        src = """
        (begin
          (define-global big (lambda [v] (shift k (call k (> v 3)))))
          (reset
            (match data
              [[(? big)] "big"]
              [else: "small"])))
        """

        expr, mod = self.compile_expr(src, data=5)
        self.assertEqual(await expr(), "big")
        expr, mod = self.compile_expr(src, data=1)
        self.assertEqual(await expr(), "small")


    async def test_koskari_extractor_flattens(self):
        src = """
        (begin
          (define-global half (lambda [v] (shift k (call k (// v 2)))))
          (reset
            (match data
              [[(app half x)] x]
              [else: "miss"])))
        """

        expr, mod = self.compile_expr(src, data=10)
        self.assertEqual(await expr(), 5)


    async def test_escaped_predicate_continuation_completes_the_match(self):
        """
        A continuation captured inside a predicate and resumed twice,
        outside its delimiter, completes the walk it interrupted both
        times: each resumption walks on from the position the capture
        held, over a sequence as over a pair spine
        """

        src = """
        (begin
          (define-global pred
            (lambda [v] (shift k (begin (setattr holder "k" k) False))))
          (define-global first
            (reset
              (match data
                [[#[(? pred) y z]] #("hit" y z)]
                [else: "miss"])))
          #(first (call holder.k True) (call holder.k True)))
        """

        class Holder:
            k = None

        expr, mod = self.compile_expr(
            src, data=(1, 2, 3), holder=Holder(), setattr=setattr)
        self.assertEqual(
            await expr(), (False, ("hit", 2, 3), ("hit", 2, 3)))

        expr, mod = self.compile_expr(
            src, data=cons(1, 2, 3, nil), holder=Holder(), setattr=setattr)
        first, (hit, y, z), (hit2, y2, z2) = await expr()
        self.assertEqual((first, hit, y.datum, z.datum), (False, "hit", 2, 3))
        self.assertEqual((hit2, y2.datum, z2.datum), ("hit", 2, 3))


    async def test_escaped_predicate_continuation_inside_a_repeat(self):
        """
        A continuation captured inside a repeat's pass and resumed
        twice: each resumption walks on with the cursor, the mark and
        the items the capture held. The REPEAT head reads its inner
        pattern in the matcher dialect, hence PREDICATE
        """

        src = """
        (begin
          (define-global pred
            (lambda [v]
              (or holder.k (shift k (begin (setattr holder "k" k) False)))))
          (define-global first
            (reset
              (match data
                [[#[(REPEAT xs (PREDICATE pred) min: 0) y z]] #("hit" xs y z)]
                [else: "miss"])))
          #(first (call holder.k True) (call holder.k True)))
        """

        class Holder:
            k = None

        expr, mod = self.compile_expr(
            src, data=(1, 2, 3), holder=Holder(), setattr=setattr)
        self.assertEqual(
            await expr(),
            (False, ("hit", [1], 2, 3), ("hit", [1], 2, 3)))

        expr, mod = self.compile_expr(
            src, data=cons(1, 2, 3, nil), holder=Holder(), setattr=setattr)
        first, (hit, xs, y, z), (hit2, xs2, y2, z2) = await expr()
        self.assertIs(first, False)
        self.assertEqual((hit, [x.datum for x in xs], y.datum, z.datum),
                         ("hit", [1], 2, 3))
        self.assertEqual((hit2, [x.datum for x in xs2], y2.datum, z2.datum),
                         ("hit", [1], 2, 3))


    async def test_escaped_continuation_inside_a_capturing_repeat(self):
        """
        A continuation captured inside a repeat pass that binds a slot,
        resumed twice: each resumption completes the pass with its own
        binding, so both give the same items
        """

        src = """
        (begin
          (define-global pred
            (lambda [v]
              (or holder.k (shift k (begin (setattr holder "k" k) False)))))
          (define-global first
            (reset
              (match data
                [[#[(REPEAT xs (MATCH (BIND "a") (PREDICATE pred)) min: 0) y]]
                 #("hit" xs y)]
                [else: "miss"])))
          #(first (call holder.k True) (call holder.k True)))
        """

        class Holder:
            k = None

        expr, mod = self.compile_expr(
            src, data=(1, 2, 3), holder=Holder(), setattr=setattr)
        self.assertEqual(
            await expr(),
            (False, ("hit", [{"a": 1}], 3), ("hit", [{"a": 1}], 3)))


    async def test_awaiting_extractor_awaits_in_place(self):
        src = """
        (match data
          [[(app half 5)] "five"]
          [[(app half x)] x]
          [else: "miss"])
        """

        async def half(v):
            await asyncio.sleep(0)
            return v // 2

        expr, mod = self.compile_expr(src, data=10, half=half)
        self.assertEqual(await expr(), "five")
        expr, mod = self.compile_expr(src, data=8, half=half)
        self.assertEqual(await expr(), 4)


# The end.
