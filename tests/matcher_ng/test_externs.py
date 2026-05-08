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
Tests for matcher extern defaults registry and Matcher.missing_externs
"""


import unittest

import pytest

from koskari.lib import (
    cons,
    get_extern_defaults,
    keyword,
    nil,
    reset_extern_defaults,
    symbol,
)
from koskari.matcher_ng import Matcher, MatchResult, thaw


pytestmark = pytest.mark.phase_infra


def _lambda_style_tuple():
    """
    Frozen matcher tuple with extern ('symbol',): TYPE(symbol) capture + REST
    """

    slots = ("name", "body")
    externs = ("symbol",)
    consts = ("::",)
    nodes = (
        ("TEST", "TYPE", 0),
        ("CAPTURE", 0, 0, 0, -1),
        ("TEST", "LITERAL", 0),
        ("TEST", "ANY", -1),
        ("TEST", "ANY", -1),
        ("REST", 1),
        ("GROUP", 1, 2, 3, 4, 5),
    )
    return ("MATCHER", 1, slots, externs, consts, nodes, 6)


def _two_extern_tuple():
    """
    Matcher declaring externs ('symbol', 'Point'); entry node uses symbol only
    """

    return (
        "MATCHER",
        1,
        (),
        ("symbol", "Point"),
        (),
        (("TEST", "TYPE", 0),),
        0,
    )


class TestExternDefaults(unittest.TestCase):

    def test_defaults_contains_baseline(self):
        """
        Baseline extern names map to the expected type objects
        """

        d = get_extern_defaults()
        sym_t = type(symbol("x"))
        key_t = type(keyword("x"))
        pair_t = type(cons(1, nil))
        self.assertIs(d["pair"], pair_t)
        self.assertIs(d["symbol"], sym_t)
        self.assertIs(d["keyword"], key_t)
        self.assertIs(d["int"], int)
        self.assertIs(d["str"], str)
        self.assertIs(d["tuple"], tuple)
        self.assertIs(d["list"], list)

    def test_match_without_kwargs_uses_defaults(self):
        """
        TYPE(symbol) matcher matches when symbol extern is resolved from
        defaults
        """

        m = thaw(_lambda_style_tuple())
        self.assertIsInstance(m, Matcher)
        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        result = m.match(subj)
        self.assertIsInstance(result, MatchResult)
        self.assertTrue(result.matched)

    def test_explicit_kwarg_overrides_default(self):
        """
        A supplied extern binding wins over the defaults dict
        """

        m = thaw(_lambda_style_tuple())
        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        bad = m.match(subj, symbol=int)
        self.assertFalse(bad.matched)
        good = m.match(subj, symbol=type(symbol("x")))
        self.assertTrue(good.matched)

    def test_runtime_mutation_persists(self):
        """
        Names added to the defaults dict are resolved like baseline entries
        """

        class Point(object):
            pass

        d = get_extern_defaults()
        d["Point"] = Point
        try:
            m = thaw(
                (
                    "MATCHER",
                    1,
                    (),
                    ("Point",),
                    (),
                    (("TEST", "TYPE", 0),),
                    0,
                )
            )
            result = m.match((Point(),))
            self.assertTrue(result.matched)
        finally:
            reset_extern_defaults()

    def test_reset_extern_defaults_restores_baseline(self):
        """
        reset_extern_defaults drops ad-hoc entries and restores baseline keys
        """

        d = get_extern_defaults()
        d["custom_extern_marker"] = object
        self.assertIn("custom_extern_marker", get_extern_defaults())
        reset_extern_defaults()
        d2 = get_extern_defaults()
        self.assertNotIn("custom_extern_marker", d2)
        self.assertIn("symbol", d2)
        self.assertIn("pair", d2)


class TestMissingExterns(unittest.TestCase):

    def test_missing_externs_basic(self):
        """
        Names absent from provided are reported in extern order
        """

        m = thaw(_two_extern_tuple())
        missing = m.missing_externs(provided={"symbol": symbol("x")})
        self.assertEqual(missing, ("Point",))

    def test_missing_externs_exclude_defaults(self):
        """
        With exclude_defaults, names satisfied by the defaults dict are omitted
        """

        m = thaw(_two_extern_tuple())
        missing = m.missing_externs(exclude_defaults=True)
        self.assertEqual(missing, ("Point",))

    def test_missing_externs_validates_provided(self):
        """
        Non-mapping provided raises TypeError
        """

        m = thaw(_two_extern_tuple())
        with self.assertRaises(TypeError):
            m.missing_externs(provided="not-a-mapping")
