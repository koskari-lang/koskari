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
tests.lib.test_match_case_env

A match site's extern env and first element test: an arm whose externs
all come from the defaults sees a change to those defaults on its
next execution, and arms the matcher rules out on the bare subject
give the same answers as the engine would, for every root shape, a
subject whose __class__ lies, and a Datum-boxed subject.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    Datum, cons, get_extern_defaults, nil, reset_extern_defaults)
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
define-class Point []:
    define __init__ [self x y]:
        set! self.x x
        set! self.y y

define typed [v]:
    match v:
        case m :: int:
            (+ 1000 m)
        case s :: str:
            (len s)
        else:
            -1

define shapes [v]:
    match v:
        case Point[x: 0 y: 0]:
            "origin"
        case Point[x: a y: b]:
            (+ a b)
        case #[a b]:
            (* a b)
        case #{"k": m}:
            m
        case m :: int when (> m 5):
            "big"
        case m :: int:
            "small"
        else:
            "other"

define which [v]:
    match v:
        case Point[x: a y: b]:
            "class"
        case #[a b]:
            "seq"
        case #{"k": m}:
            "map"
        case m :: int:
            "int"
        else:
            "other"
"""


class TestMatchCaseEnv(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("match_case_env")
        vars(module)["__file__"] = "<match_case_env>"
        await load_module_source_async(module, SRC)
        return vars(module)

    async def test_defaults_change_reaches_a_running_site(self):
        mod = await self.load()
        typed = mod["typed"]
        self.assertEqual(await typed(5), 1005)
        self.assertEqual(await typed(2.5), -1)
        defaults = get_extern_defaults()
        saved = defaults["int"]
        try:
            defaults["int"] = float
            self.assertEqual(await typed(2.5), 1002.5)
            self.assertEqual(await typed(5), -1)
            defaults["int"] = saved
            self.assertEqual(await typed(5), 1005)
            reset_extern_defaults()
            self.assertEqual(await typed(5), 1005)
            self.assertEqual(await typed("abc"), 3)
        finally:
            reset_extern_defaults()

    async def test_every_root_shape_answers_as_the_engine(self):
        mod = await self.load()
        shapes = mod["shapes"]
        Point = mod["Point"]
        cases = [
            (Point(0, 0), "origin"),
            (Point(3, 4), 7),
            ([6, 7], 42),
            ((2, 3), 6),
            ({"k": 9, "extra": True}, 9),
            ({"other": 1}, "other"),
            (17, "big"),
            (2, "small"),
            ("hello", "other"),
            (2.5, "other"),
            ([1, 2, 3], "other"),
            (None, "other"),
        ]
        for subject, expected in cases:
            with self.subTest(subject=subject):
                self.assertEqual(await shapes(subject), expected)

    async def test_subject_whose_class_lies_matches_by_isinstance(self):
        mod = await self.load()
        Point = mod["Point"]

        class Liar(object):
            x = 1
            y = 2

            @property
            def __class__(self):
                return Point

        self.assertEqual(await mod["shapes"](Liar()), 3)

    async def test_datum_boxed_subject_is_peeled(self):
        mod = await self.load()
        which = mod["which"]
        Point = mod["Point"]
        self.assertEqual(await which(Datum(cons(6, 7, nil))), "seq")
        self.assertEqual(await which(Datum({"k": 1})), "map")
        # a class pattern tests the element as it is, so a boxed
        # instance is not one of the class: the engine's own rule
        self.assertEqual(await which(Datum(Point(1, 2))), "other")
        self.assertEqual(await which(Point(1, 2)), "class")
        self.assertEqual(await which(Datum(4)), "int")
        self.assertEqual(await which(Datum("s")), "other")


# The end.
