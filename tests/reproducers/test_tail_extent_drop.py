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
tests.reproducers.test_tail_extent_drop

Full-stack reproducer: a promoted tail call from inside a let binding
must uninstall the discardable NS extent at the frame handoff instead
of stranding one node per iteration on the dynamic chain until the
final return

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'tail_extent_drop.ki')


class TestTailExtentDrop(unittest.IsolatedAsyncioTestCase):
    """
    Promoted tail calls through let uninstall the NS extent per hop
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.tail_extent_drop')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_deep_tail_loop_through_let_completes(self):
        mod = await self.load_module()
        result = await mod.quiet_loop(200000)
        self.assertEqual(result, 0)


    async def test_chain_holds_one_ns_node_after_many_hops(self):
        # each promoted hop opens a fresh let; a raise at the bottom
        # records the live chain, which must hold only the final hop's
        # NS node -- stranded nodes from earlier hops would all appear
        mod = await self.load_module()
        try:
            await mod.raising_loop(200)
        except ValueError as exc:
            chain = getattr(exc, "__koskari_dynchain__", None)
            self.assertIsNotNone(chain)
            ns_nodes = [m for m in chain if m.get("kind") == "NS"]
            self.assertLessEqual(len(ns_nodes), 1)
        else:
            self.fail("raising_loop did not raise")


# The end.
