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
The one function a Syntax dispatches through: every arm's matcher in
one Function, the index and captures of the first arm that matches
its result, whether the arm's matcher lowers inline from a frozen
tuple or calls a function a module built

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import DatumFrame, ModuleNamespace, cons, nil, symbol
from koskari.match_code import compile_match_function
from koskari.matcher import (
    CALLEDBY, CAPTURE, LITERAL, MATCH, SYMBOL, TYPE, VALUE, Dispatcher,
    MatchCompiler, Matcher)
from koskari.syntax import SpecialTransform, Syntax


pytestmark = pytest.mark.phase_infra


def function_matcher(pattern):
    """
    A Matcher over a standalone function, as a module's build row
    hands one to build-syntax
    """

    ns = ModuleNamespace("module", {})
    fn = compile_match_function(MatchCompiler().lower(pattern), ns)
    return Matcher.from_function(fn, tuple(MatchCompiler().lower(pattern)[1]))


def form(*items):
    return cons(*items, nil)


class TestDispatcher(unittest.TestCase):

    def test_first_arm_wins(self):
        d = Dispatcher((
            MatchCompiler.compile(MATCH(LITERAL(42))),
            MatchCompiler.compile(MATCH(LITERAL(99)))))
        idx, captures = d.select((42,))
        self.assertEqual(idx, 0)
        self.assertEqual(captures, {})
        idx, captures = d.select((99,))
        self.assertEqual(idx, 1)
        self.assertEqual(captures, {})


    def test_no_arm_matches(self):
        d = Dispatcher((MatchCompiler.compile(MATCH(LITERAL(42))),))
        self.assertEqual(d.select((1,)), (-1, None))
        self.assertEqual(d.select(nil), (-1, None))


    def test_function_arms_between_inline_arms(self):
        called = symbol("f")
        d = Dispatcher((
            MatchCompiler.compile(MATCH(CALLEDBY(), SYMBOL("s"), VALUE("e"))),
            function_matcher(MATCH(CALLEDBY(), VALUE("one"))),
            MatchCompiler.compile(MATCH(CALLEDBY()))))

        idx, captures = d.select(form(called, symbol("x"), 1))
        self.assertEqual(idx, 0)
        self.assertEqual(captures["s"].datum, symbol("x"))

        idx, captures = d.select(form(called, 1))
        self.assertEqual(idx, 1)
        self.assertIsInstance(captures, DatumFrame)
        self.assertEqual(captures["one"].datum, 1)

        idx, captures = d.select(form(called))
        self.assertEqual(idx, 2)
        self.assertEqual(captures["called-by"].datum, called)

        self.assertEqual(d.select(form(called, 1, 2, 3)), (-1, None))


    def test_function_arm_hit_with_no_captures(self):
        """
        A function arm's empty captures are a hit, not a miss
        """

        d = Dispatcher((
            function_matcher(MATCH(LITERAL(1))),
            MatchCompiler.compile(MATCH(VALUE("x")))))
        idx, captures = d.select((1,))
        self.assertEqual(idx, 0)
        self.assertEqual(captures, {})


    def test_core_type_arms_read_no_extern(self):
        matchers = (
            MatchCompiler.compile(MATCH(CAPTURE("k", TYPE("keyword")))),
            MatchCompiler.compile(MATCH(CAPTURE("s", TYPE("symbol")))))
        self.assertEqual([m.externs for m in matchers], [(), ()])
        d = Dispatcher(matchers)
        idx, captures = d.select((symbol("a"),))
        self.assertEqual(idx, 1)
        self.assertEqual(captures["s"], symbol("a"))


    def test_inline_arm_with_an_extern_is_refused(self):
        """
        A syntax passes no externs, so an arm whose pattern names a user
        type has no binding for it and the dispatcher refuses it when
        built, naming the extern
        """

        with self.assertRaises(TypeError) as caught:
            Dispatcher((
                MatchCompiler.compile(MATCH(CAPTURE("p", TYPE("Point")))),))
        self.assertIn("Point", str(caught.exception))


    def test_improper_subject_refused(self):
        d = Dispatcher((MatchCompiler.compile(MATCH(CALLEDBY())),))
        with self.assertRaises(TypeError):
            d.select(cons(symbol("f"), 1))


    def test_clauses_must_be_matchers(self):
        with self.assertRaises(TypeError):
            Dispatcher((MatchCompiler.compile(MATCH(LITERAL(1))), "no"))


class TestSyntaxDispatch(unittest.TestCase):

    def test_syntax_builds_its_function_once(self):
        name = symbol("compile/d")
        s = Syntax(name, (
            SpecialTransform(
                MATCH(CALLEDBY(), VALUE("x")),
                lambda c, src, mr: ("one", mr.captures["x"].datum)),
            SpecialTransform(
                MATCH(CALLEDBY()),
                lambda c, src, mr: "none")))
        self.assertIs(s.dispatcher, s.dispatcher)
        self.assertEqual(s.compile_impl(None, form(name, 5)), ("one", 5))
        self.assertEqual(s.compile_impl(None, form(name)), "none")

        tf, mr = s.match_transformer(form(name, 5))
        self.assertIs(tf, s.transforms[0])
        self.assertTrue(mr.matched)
        self.assertEqual(mr.captures["x"].datum, 5)
        self.assertEqual(s.match_transformer(form(name, 1, 2)), (None, None))


    def test_composed_syntax_dispatches_across_both(self):
        """
        An extension prepended with + is tried first, and the composed
        syntax has a function of its own over every arm
        """

        name = symbol("compile/d")
        base = Syntax(name, (
            SpecialTransform(
                MATCH(CALLEDBY(), VALUE("x")),
                lambda c, src, mr: "base"),))
        extension = Syntax(name, (
            SpecialTransform(
                MATCH(CALLEDBY(), LITERAL(1)),
                lambda c, src, mr: "extension"),
            SpecialTransform(
                MATCH(CALLEDBY(), VALUE("a"), VALUE("b")),
                lambda c, src, mr: "wide"),))
        composed = base + extension
        self.assertEqual(composed.compile_impl(None, form(name, 1)), "extension")
        self.assertEqual(composed.compile_impl(None, form(name, 2)), "base")
        self.assertEqual(composed.compile_impl(None, form(name, 1, 2)), "wide")
        self.assertEqual(base.compile_impl(None, form(name, 1)), "base")
        self.assertIsNot(composed.dispatcher, base.dispatcher)
        self.assertEqual(len(composed.dispatcher.matchers), 3)


# The end.
