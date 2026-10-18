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
tests.reproducers.test_match_core_repro

A matcher keeps its walk's captures on the operand stack: a
continuation captured by a Koskari predicate inside a REPEAT, a ONEOF
or an OPTIONAL and resumed more than once completes with the captures
of its own resumption only. A match arm writes a module variable only
for a slot it filled

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.matcher import (
    CAPTURE, MATCH, ONEOF, OPTIONAL, PREDICATE, REST, repeat_capture)
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


GATE_SRC = """
(define-class Holder []
  (define k None))

(define holder (Holder))

(define gate [v]
  (or holder.k (shift k (begin (setattr holder "k" k) False))))

(define run [m data]
  (reset (m data)))

(define resume [v]
  (call holder.k v))
"""


OPTIONAL_SRC = """
(define b "default")

(define hit-short
  (match short
    [[#[a (OPTIONAL (BIND "b"))]] #(a b)]
    [else: "else"]))

(define after-short b)

(define missed
  (match "x"
    [[#[a (OPTIONAL (BIND "b"))]] #(a b)]
    [else: "else"]))

(define after-missed b)

(define x "default")

(define or-hit
  (match #(5 2)
    [[(or #[x 1] #[5 2])] "hit"]
    [else: "else"]))

(define after-or-hit x)
"""


class TestResumedContinuations(unittest.IsolatedAsyncioTestCase):
    """
    The predicate gate shifts out of the walk the first time it runs,
    and answers with the value its continuation is resumed with; once
    a continuation is held, later calls answer true
    """

    async def load(self):
        module = new_module("match_core_resume")
        vars(module)["__file__"] = "<match_core_resume>"
        await load_module_source_async(module, GATE_SRC)
        return vars(module)


    async def resumed(self, pattern, subject, answers):
        mod = await self.load()
        m = pattern.build({"gate": mod["gate"]})
        first = await mod["run"](m, subject)
        results = [await mod["resume"](answer) for answer in answers]
        return first, results


    async def test_inside_a_repeat(self):
        pattern = MATCH(
            CAPTURE("a"),
            repeat_capture("xs", MATCH(CAPTURE("p"), PREDICATE("gate"))),
            REST("r"))
        first, results = await self.resumed(
            pattern, (1, 2, 3, 4, 5), (True, False, True))
        self.assertIs(first, False)
        self.assertEqual(results, [
            {"a": 1, "xs": [{"p": 2}, {"p": 4}], "r": ()},
            {"a": 1, "xs": [], "r": (2, 3, 4, 5)},
            {"a": 1, "xs": [{"p": 2}, {"p": 4}], "r": ()},
        ])


    async def test_inside_a_oneof(self):
        pattern = MATCH(ONEOF(
            MATCH(PREDICATE("gate"), CAPTURE("x")),
            MATCH(CAPTURE("y"), CAPTURE("z"))))
        first, results = await self.resumed(
            pattern, (1, 2), (False, True, False))
        self.assertIs(first, False)
        self.assertEqual(results, [
            {"y": 1, "z": 2},
            {"x": 2},
            {"y": 1, "z": 2},
        ])


    async def test_inside_an_optional(self):
        pattern = MATCH(
            CAPTURE("a"),
            OPTIONAL(MATCH(PREDICATE("gate"), CAPTURE("b"))),
            CAPTURE("c", REST()))
        first, results = await self.resumed(
            pattern, (1, 2, 3), (True, False, True))
        self.assertIs(first, False)
        self.assertEqual(results, [
            {"a": 1, "b": 3, "c": ()},
            {"a": 1, "c": (2, 3)},
            {"a": 1, "b": 3, "c": ()},
        ])


class TestModuleVariables(unittest.IsolatedAsyncioTestCase):
    """
    A module variable named like a capture keeps its value through an
    OPTIONAL that did not fire and a missed arm that did not store it,
    and holds what an or branch stored before it missed
    """

    async def test_unbound_slots_leave_module_variables(self):
        module = new_module("match_core_module_vars")
        vars(module)["__file__"] = "<match_core_module_vars>"
        vars(module)["short"] = (1,)
        await load_module_source_async(module, OPTIONAL_SRC)
        mod = vars(module)
        self.assertEqual(mod["hit-short"], (1, "default"))
        self.assertEqual(mod["after-short"], "default")
        self.assertEqual(mod["missed"], "else")
        self.assertEqual(mod["after-missed"], "default")
        self.assertEqual(mod["or-hit"], "hit")
        self.assertEqual(mod["after-or-hit"], 5)


# The end.
