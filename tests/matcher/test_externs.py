# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for the externs a pattern's matcher function is built over
"""


import unittest

import pytest

from koskari.lib import demand, symbol
from koskari.matcher import (
    APP, CAPTURE, LITERAL, MATCH, PREDICATE, REST, TYPE, VALUE, make_matcher)


pytestmark = pytest.mark.phase_infra


LAMBDA_STYLE = MATCH(CAPTURE("name", TYPE("Kind")), REST("body"))


class TestKeywordExterns(unittest.TestCase):

    def test_every_extern_is_given_at_build(self):
        """
        A declared extern must be given when the matcher is built, and
        a matcher built without it is refused; two matchers over one
        pattern each read their own externs, and the call takes no
        keywords
        """

        with self.assertRaises(TypeError) as caught:
            make_matcher(LAMBDA_STYLE)
        self.assertIn("Kind", str(caught.exception))

        m = make_matcher(LAMBDA_STYLE, {"Kind": symbol})
        other = make_matcher(LAMBDA_STYLE, {"Kind": int})
        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        self.assertIsNotNone(demand(m)(subj))
        self.assertIsNone(demand(other)(subj))
        self.assertIsNotNone(demand(m)(subj))
        with self.assertRaises(TypeError):
            demand(m)(subj, symbol=symbol)

    def test_extern_named_subject(self):
        """
        An extern spelled subject is an extern like any other: it does
        not collide with the function's own formal, and its value
        reaches the match as an extern
        """

        m = make_matcher(MATCH(TYPE("subject")), {"subject": int})
        self.assertIsNotNone(demand(m)((7,)))
        self.assertIsNone(demand(m)(("s",)))


    def test_type_extern_named_match_subject(self):
        """
        A TYPE extern spelled match-subject reads its extern value, not
        the subject the matcher function takes
        """

        m = make_matcher(MATCH(TYPE("match-subject")), {"match-subject": int})
        self.assertIsNotNone(demand(m)((7,)))
        self.assertIsNone(demand(m)(("s",)))


    def test_app_extern_named_match_subject(self):
        """
        An APP extractor spelled match-subject is called on the element,
        not replaced by the subject the matcher function takes
        """

        m = make_matcher(
            MATCH(APP("match-subject", VALUE("a"))),
            {"match-subject": lambda v: v * 10})
        hit = demand(m)((3,))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["a"], 30)


    def test_user_type_extern(self):
        class Point:
            pass

        m = make_matcher(MATCH(TYPE("Point")), {"Point": Point})
        self.assertIsNotNone(demand(m)((Point(),)))
        self.assertIsNone(demand(m)((7,)))


class TestExternsPerMatcher(unittest.TestCase):

    def test_a_predicate_matching_with_another_matcher(self):
        """
        Two matchers over one pattern each read their own externs: an
        outer predicate that runs the inner matcher mid-walk leaves the
        outer walk reading the outer predicate
        """

        pattern = MATCH(PREDICATE("p"), PREDICATE("p"))
        inner = make_matcher(pattern, {"p": lambda y: False})
        seen = []

        def outer_p(x):
            seen.append(x)
            if x == 1:
                self.assertIsNone(demand(inner)((9, 9)))
            return True

        outer = make_matcher(pattern, {"p": outer_p})
        self.assertIsNotNone(demand(outer)((1, 2)))
        self.assertEqual(seen, [1, 2])


class TestKeywordsAreIgnoredByName(unittest.TestCase):

    def test_zero_extern_matcher_ignores_keywords(self):
        """
        A name in the externs naming no extern of a matcher is ignored,
        whether or not the matcher declares any externs
        """

        m = make_matcher(MATCH(LITERAL(42)), {"bogus": 1})
        self.assertIsNotNone(demand(m)((42,)))


# The end.
