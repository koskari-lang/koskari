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
tests.reproducers.test_generator_source_exhaustion

Full-stack reproducer for the providers that catch a source running
out rather than propagating it, over koskari generator sources.

A koskari generator exhausts on the in-flight path: the step frame is
itself the live frame when the source runs out, so reaching the
provider's own resume depends on process_raise handing that exhaustion
to the prologue rather than unwinding past the frame. batched must
still emit a short final batch; zip-longest must fill for the spent
source and carry the row on while a sibling is live.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'generator_source_exhaustion.ki')


class TestBatchedGeneratorSource(unittest.IsolatedAsyncioTestCase):
    """
    batched over a koskari generator keeps every element the source
    produced
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.generator_source_exhaustion')
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


class TestZipLongestGeneratorSource(unittest.IsolatedAsyncioTestCase):
    """
    zip-longest over a koskari generator fills for a spent source
    rather than ending the row
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.generator_source_exhaustion')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod

    async def test_first_source_runs_out(self):
        mod = await self.load_module()
        self.assertEqual(await mod.first_source_runs_out(),
                         [(1, 10), (None, 20), (None, 30)])

    async def test_second_source_runs_out(self):
        mod = await self.load_module()
        self.assertEqual(await mod.second_source_runs_out(),
                         [(1, 10), (2, None), (3, None)])

    async def test_sources_run_out_together(self):
        mod = await self.load_module()
        self.assertEqual(await mod.sources_run_out_together(),
                         [(1, 10), (2, 20)])

    async def test_fillvalue(self):
        mod = await self.load_module()
        self.assertEqual(await mod.filled_value(),
                         [(1, 10), (2, 0), (3, 0)])


class TestCatchingProvidersUnderExtents(unittest.IsolatedAsyncioTestCase):
    """
    A provider that catches exhaustion produces the same thing inside
    a delimited extent and through an invoked continuation
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.generator_source_exhaustion')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod

    async def test_batched_under_reset(self):
        mod = await self.load_module()
        self.assertEqual(await mod.batched_under_reset(),
                         [(0, 1, 2), (3, 4, 5), (6,)])

    async def test_batched_through_an_invoked_continuation(self):
        mod = await self.load_module()
        self.assertEqual(await mod.batched_via_continuation(),
                         [(0, 1), (2,)])

    async def test_zip_longest_under_reset(self):
        mod = await self.load_module()
        self.assertEqual(await mod.zip_longest_under_reset(),
                         [(1, 10), (None, 20), (None, 30)])


# The end.
