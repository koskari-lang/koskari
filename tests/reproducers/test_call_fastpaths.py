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
tests.reproducers.test_call_fastpaths

Full-stack reproducers for the call and return fast paths: self-tail
in-place namespace rebind, plain returns delivered into loop / let /
foreach extents, unbound method calls through the cached-frame steal,
and method-cache shadow detection after the receiver type's shared keys
gain the method's name or the instance materializes a dict-only shadow

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'call_fastpaths.ki')


class TestCallFastPaths(unittest.IsolatedAsyncioTestCase):
    """
    Call install and return delivery fast paths
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.call_fastpaths')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_self_tail_inplace_deep(self):
        # after the first promoted hop the frame's FN tag matches the
        # callee, so every following hop takes the in-place namespace
        # rebind; a broken clear/steal pairing would corrupt acc or n
        mod = await self.load_module()
        n = 200000
        result = await mod.tail_sum(n, 0)
        self.assertEqual(result, n * (n + 1) // 2)


    async def test_return_into_while_extent(self):
        mod = await self.load_module()
        n = 5000
        result = await mod.sum_calls_while(n)
        self.assertEqual(result, n * (n + 1) // 2)


    async def test_return_into_foreach_extent(self):
        mod = await self.load_module()
        n = 5000
        result = await mod.sum_calls_foreach(n)
        self.assertEqual(result, n * (n + 1) // 2)


    async def test_return_into_let_extent(self):
        mod = await self.load_module()
        n = 5000
        result = await mod.sum_calls_let(n)
        self.assertEqual(result, n * (n + 1) // 2)


    async def test_unbound_method_calls_in_loop(self):
        mod = await self.load_module()
        c = mod.Counter()
        n = 10000
        result = await mod.bump_loop(c, n)
        self.assertEqual(result, n * (n - 1) // 2)
        self.assertEqual(c.total, result)


    async def test_instance_shadow_seen_after_site_warm(self):
        # warm the call site against unshadowed instances so the method
        # cache memoizes the class method and records the name absent
        # from the type's shared keys, then grow the keys with an
        # instance attribute of the same name: the next call through the
        # same site must see the shadow, not the memo
        mod = await self.load_module()
        c = mod.Counter()
        for _ in range(64):
            await mod.call_bump(c, 1)

        shadowed = mod.Counter()
        shadowed.bump = lambda v: ('shadowed', v)
        self.assertEqual(await mod.call_bump(shadowed, 7), ('shadowed', 7))

        # the previously warmed instance gains a shadow too
        c.bump = lambda v: ('late', v)
        self.assertEqual(await mod.call_bump(c, 9), ('late', 9))

        # deleting the shadow restores the class method
        del shadowed.bump
        self.assertEqual(await mod.call_bump(shadowed, 3), 3)


    async def test_small_capacity_instance_probes_grown_keys(self):
        # 3.13+ sizes an instance's inline values from the shared keys
        # at creation, so a keys index added later can exceed an early
        # instance's buffer; the cached call site's shadow probe for
        # such a name must bound its read by the receiver's capacity
        # (ASAN catches the miss) and resolve to the class method
        mod = await self.load_module()
        early = mod.Counter()
        for _ in range(8):
            await mod.call_bump(early, 1)

        # grow the shared keys one name at a time through fresh
        # instances, each sized to the keys of its moment, so the keys
        # advance well past the early instance's capacity
        for k in range(16):
            g = mod.Counter()
            setattr(g, 'grow_%02d' % k, k)

        # 'bump' joins the keys at an index beyond early's buffer
        shadowed = mod.Counter()
        shadowed.bump = lambda v: ('shadowed', v)
        self.assertEqual(await mod.call_bump(shadowed, 2), ('shadowed', 2))

        # the early instance has no shadow: the class method runs, and
        # a late-keys name absent from it raises cleanly
        self.assertEqual(await mod.call_bump(early, 3), early.total)
        with self.assertRaises(AttributeError):
            early.grow_00


    async def test_dict_materialized_shadow_seen(self):
        # materialize the instance dict and plant the shadow through it
        # directly: on 3.13+ a materialized dict can coexist with valid
        # inline values, and a dict insert outside the shared keys must
        # detach the values so the cached call site consults the dict
        mod = await self.load_module()
        c = mod.Counter()
        for _ in range(64):
            await mod.call_bump(c, 1)

        c.__dict__['bump'] = lambda v: ('dict-shadow', v)
        self.assertEqual(await mod.call_bump(c, 5), ('dict-shadow', 5))


# The end.
