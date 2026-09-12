# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.

"""
tests.reproducers.test_batched_generator_source

Full-stack reproducer for a native batched over a koskari generator.
A source that runs out part way through a batch still emits the
elements it did produce.

A koskari generator exhausts on the in-flight path: the batched step
frame is itself the live frame when the source runs out, so the short
final batch depends on the raise walk handing that exhaustion to the
resume armed on the frame rather than popping past it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'batched_generator_source.ki')


class TestBatchedGeneratorSource(unittest.IsolatedAsyncioTestCase):
    """
    batched over a koskari generator keeps every element the source
    produced
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.batched_generator_source')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod

    async def test_even_division(self):
        mod = await self.load_module()
        self.assertEqual(await mod.even_division(),
                         [(0, 1, 2), (3, 4, 5)])

    async def test_short_final_batch_survives(self):
        mod = await self.load_module()
        self.assertEqual(await mod.short_tail(),
                         [(0, 1, 2), (3, 4, 5), (6,)])

    async def test_whole_source_shorter_than_one_batch(self):
        mod = await self.load_module()
        self.assertEqual(await mod.whole_source_is_one_short_batch(),
                         [(0, 1)])

    async def test_no_element_is_lost(self):
        mod = await self.load_module()
        self.assertEqual(await mod.flattened(), [0, 1, 2, 3, 4])

    async def test_source_failure_inside_a_batch_propagates(self):
        mod = await self.load_module()
        with self.assertRaises(ValueError):
            await mod.raise_inside_first_batch()

    async def test_source_failure_on_a_batch_boundary_propagates(self):
        mod = await self.load_module()
        with self.assertRaises(ValueError):
            await mod.raise_on_a_batch_boundary()


# The end.
