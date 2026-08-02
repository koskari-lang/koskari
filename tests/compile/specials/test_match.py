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

from koskari.exceptions import SpecialError

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


@dataclass
class Point:
    x: int
    y: int


class TestMatchSpecial(SpecialsTestCase):

    async def test_match_no_clauses(self):
        expr, mod = self.compile_expr("(compile/match 5)")
        self.assertEqual(await expr(), None)


    async def test_match_else_only(self):
        expr, mod = self.compile_expr("(compile/match 5 [else: 100])")
        self.assertEqual(await expr(), 100)


    async def test_match_literal(self):
        src = """
        (compile/match data
          [[1] "one"]
          [[5] "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src, data=5)
        self.assertEqual(await expr(), "five")


    async def test_match_literal_no_match_to_else(self):
        src = """
        (compile/match data
          [[1] "one"]
          [[5] "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src, data=9)
        self.assertEqual(await expr(), "meh")


    async def test_match_capture_whole_value(self):
        src = """
        (compile/match data
          [[x] x])
        """
        expr, mod = self.compile_expr(src, data=42)
        self.assertEqual(await expr(), 42)


    async def test_match_typed_multi_token(self):
        src = """
        (compile/match data
          [[x :: int] x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data=7, int=int)
        self.assertEqual(await expr(), 7)


    async def test_match_typed_multi_token_no_match(self):
        src = """
        (compile/match data
          [[x :: int] x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data="hello", int=int)
        self.assertEqual(await expr(), "nope")


    async def test_match_typed_single_form(self):
        src = """
        (compile/match data
          [[(x :: int)] x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data=7, int=int)
        self.assertEqual(await expr(), 7)


    async def test_match_sequence(self):
        src = """
        (compile/match data
          [[#[a b c]] b]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 2)


    async def test_match_sequence_wrong_length_falls_through(self):
        src = """
        (compile/match data
          [[#[a b]] b]
          [else: 99])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 99)


    async def test_match_sequence_rest(self):
        src = """
        (compile/match data
          [[#[a *: rest]] rest]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), (2, 3))


    async def test_match_class(self):
        src = """
        (compile/match data
          [[(item Point a b)] b]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(
            src, data=Point(3, 4), Point=Point)
        self.assertEqual(await expr(), 4)


    async def test_match_class_no_match(self):
        src = """
        (compile/match data
          [[(item Point a b)] b]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data="zzz", Point=Point)
        self.assertEqual(await expr(), "nope")


    async def test_match_or(self):
        src = """
        (compile/match data
          [[(or 1 2)] "low"]
          [else: "high"])
        """
        expr, mod = self.compile_expr(src, data=2)
        self.assertEqual(await expr(), "low")


    async def test_match_predicate(self):
        src = """
        (compile/match data
          [[(? even)] "even"]
          [else: "odd"])
        """
        even = lambda v: v % 2 == 0  # noqa: E731
        expr, mod = self.compile_expr(src, data=4, even=even)
        self.assertEqual(await expr(), "even")
        expr, mod = self.compile_expr(src, data=5, even=even)
        self.assertEqual(await expr(), "odd")


    async def test_match_app_extraction(self):
        src = """
        (compile/match data
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
        (compile/match data
          [[#[a b]] "two"]
          [[#[a b c]] "three"]
          [else: "other"])
        """
        expr, mod = self.compile_expr(src, data=(1, 2))
        self.assertEqual(await expr(), "two")


    async def test_match_bare_multi_token_raises(self):
        src = """
        (compile/match data
          [[a b c] "seq?"]
          [else: 0])
        """
        with self.assertRaises(SpecialError):
            self.compile_expr(src, data=(1, 2, 3))


class TestMatchGuards(SpecialsTestCase):
    """
    when: / unless: guards run after the pattern binds; a failed guard
    advances to the next arm with the subject intact.
    """

    async def test_when_guard_passes(self):
        src = """
        (compile/match data
          [[x :: int] when: (big x) "big"]
          [else: "other"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=50, int=int, big=big)
        self.assertEqual(await expr(), "big")


    async def test_when_guard_fails_advances(self):
        src = """
        (compile/match data
          [[x :: int] when: (big x) "big"]
          [[y :: int] "small"]
          [else: "other"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=5, int=int, big=big)
        self.assertEqual(await expr(), "small")


    async def test_unless_guard(self):
        src = """
        (compile/match data
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
        (compile/match data
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
        (compile/match data
          [[x] when: (big x) "big"]
          [else: "meh"])
        """
        big = lambda v: v > 10  # noqa: E731
        expr, mod = self.compile_expr(src, data=5, big=big)
        self.assertEqual(await expr(), "meh")


    async def test_guard_missing_expression_raises(self):
        src = """
        (compile/match data
          [[x] when:])
        """
        with self.assertRaises(SpecialError):
            self.compile_expr(src, data=5)


class TestMatchLayout(SpecialsTestCase):
    """
    End-to-end tests through the match/case block layout, which wraps
    each case header into the clause delimiter for compile/match.
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
        (compile/let [[T int]]
          (lambda [data]
            (compile/match data
              [[x :: T] "isT"]
              [else: "no"])))
        """
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertEqual(await fn(7), "isT")
        self.assertEqual(await fn("hi"), "no")


# The end.
