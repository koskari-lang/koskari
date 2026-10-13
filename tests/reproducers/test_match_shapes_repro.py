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
tests.reproducers.test_match_shapes_repro

Full-stack reproducer: runtime match/case over a @dataclass class
shape hierarchy (Point, Plane, Square, Cube). One consumer module
exercises the whole pattern vocabulary end to end -- class patterns via
__match_args__, keyword-field literals, when/unless guards, or-patterns,
sequence patterns with rest, mapping patterns with literal and computed
keys, app extraction, typed captures, and the string-atom rule.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'match_shapes.ki')


class TestMatchShapesReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Runtime match/case driven through a dataclass shape hierarchy.
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.match_shapes')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_area_class_dispatch(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_area())


    async def test_describe_keyword_fields_and_guards(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_describe())


    async def test_or_pattern_shared_slot(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_or_pattern())


    async def test_sequence_patterns_and_string_atom_rule(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_sequences())


    async def test_mapping_patterns(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_mappings())


    async def test_mapping_computed_key(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_computed_key())


    async def test_app_extraction_with_guard(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_extraction())


# The end.
