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
Structural tests for freeze-built hygiene trees and tree walks
"""


import unittest

import pytest

from koskari.hygiene import Hygiene, validate_hygiene_tree, walk_hygiene_tree
from koskari.lib import Datum, symbol
from koskari.matcher import (
    BINDINGS,
    BODY,
    DEFINE,
    MATCH,
    VALUE,
    HygienicMatchCompiler,
    repeat_capture,
    binding_inner_pattern,
)


pytestmark = pytest.mark.phase_infra


class TestValidateHygiene(unittest.TestCase):


    def test_validate_accepts_minimal_tree(self):
        """
        validate_hygiene_tree accepts a single root node
        """

        tree = Hygiene.single_node()
        validate_hygiene_tree(tree)


    def test_validate_rejects_duplicate_node_id(self):
        """
        Duplicate node ids are a structural error
        """

        node = Hygiene.single_node().root
        dup = Hygiene(
            (node, node),
            (),
            0)
        with self.assertRaises(ValueError):
            validate_hygiene_tree(dup)


class TestHygieneLower(unittest.TestCase):


    def test_define_value_registers_region(self):
        """
        DEFINE plus VALUE lowers a scope region on the root node
        """

        pat = MATCH(DEFINE("name"), VALUE("value"))
        hc = HygienicMatchCompiler()
        hc.lower(pat)
        tree = hc.build_hygiene()
        validate_hygiene_tree(tree)
        self.assertIn("name", tree.root.binders)
        self.assertIn("value", tree.root.regions)


    def test_let_bindings_scopes_body_not_duplicate_region(self):
        """
        BINDINGS scope:body owns body walk; BODY is not also registered
        """

        pat = MATCH(
            BINDINGS("bindings", mode="parallel", scope="body"),
            BODY("body"))
        hc = HygienicMatchCompiler()
        hc.lower(pat)
        tree = hc.build_hygiene()
        validate_hygiene_tree(tree)
        self.assertIn("bindings", tree.root.binders)
        self.assertIn("body", tree.root.scoped_slots)
        self.assertNotIn("body", tree.root.regions)


    def test_repeat_capture_has_repeat_element_edge(self):
        """
        repeat_capture lowers a repeat_element edge for row hygiene
        """

        pat = MATCH(repeat_capture("bindings", binding_inner_pattern()))
        hc = HygienicMatchCompiler()
        hc.lower(pat)
        tree = hc.build_hygiene()
        validate_hygiene_tree(tree)
        kinds = {e.kind for e in tree.edges}
        self.assertIn("repeat_element", kinds)
        rep = [e for e in tree.edges if e.kind == "repeat_element"][0]
        self.assertEqual(rep.capture_slot, "bindings")


    def test_duplicate_body_region_in_same_node_raises(self):
        """
        Two BODY slots with the same name in one frame raise at freeze
        """

        pat = MATCH(BODY("body"), BODY("body"))
        hc = HygienicMatchCompiler()
        with self.assertRaises(ValueError):
            hc.lower(pat)


class TestHygieneWalk(unittest.TestCase):


    def test_repeat_walk_visits_each_row(self):
        """
        REPEAT list captures walk one hygiene child per frame dict
        """

        pat = MATCH(repeat_capture("bindings", binding_inner_pattern()))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        result = m.match(((sa, 1), (sb, 2)), symbol=symbol)
        self.assertTrue(result.matched)
        hc = HygienicMatchCompiler()
        hc.lower(pat)
        tree = hc.build_hygiene()
        tracer = {}
        walk_hygiene_tree(
            tree,
            tree.root_id,
            result.captures,
            lambda h: None,
            tracer)
        self.assertEqual(len(tracer.get("repeat_element", [])), 2)


# The end.
