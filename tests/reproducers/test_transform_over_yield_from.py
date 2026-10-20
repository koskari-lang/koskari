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
tests.reproducers.test_transform_over_yield_from

Defect reproducer, RED until fixed: a transform stepping a koskari
generator that delegates with yield-from must deliver every value the
delegate produces. A generator suspended inside yield-from carries the
node's phase resume, and the install has to bounce through the
prologue so that resume runs. KosContext_NextResume decided on code
alone where every other consumer of the same outcome also requires no
resume set, so it threaded straight into the frame, the delivery
never happened, and the step reported exhaustion. The loop simply
ended early, with no error.

The plain for-each cases are controls: ITER_NEXT asks
kos_iter_installed and has always been right, so a regression that
breaks both points somewhere other than the predicate.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

define inner []:
  yield 1
  (sleep 0)
  yield 2
  (sleep 0)
  yield 3

define outer []:
  yield-from (inner)

define drain [src]:
  define out (#list)
  for-each [v src]:
    (out.append v)
  out

define fresh-mapped []:
  (drain (map identity (outer)))

define fresh-plain []:
  (drain (outer))

define stepped-mapped []:
  define g (outer)
  define first (await ((g.__aiter__).__anext__))
  #(first (await (drain (map identity g))))

define stepped-plain []:
  define g (outer)
  define first (await ((g.__aiter__).__anext__))
  #(first (await (drain g)))
"""


class TestTransformOverYieldFrom(unittest.IsolatedAsyncioTestCase):
    """
    map over a delegating generator delivers what for-each delivers
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_fresh_delegating_generator_through_map(self):
        """
        map over a generator whose body is a yield-from yields every
        value, not just the one before the delegate first suspends
        """

        mod = await self.load("tyf_fresh_mapped")
        self.assertEqual(await vars(mod)["fresh-mapped"](), [1, 2, 3])

    async def test_fresh_delegating_generator_plain(self):
        """
        control: the same source under for-each
        """

        mod = await self.load("tyf_fresh_plain")
        self.assertEqual(await vars(mod)["fresh-plain"](), [1, 2, 3])

    async def test_stepped_delegating_generator_through_map(self):
        """
        a generator already suspended inside its yield-from delivers its
        remaining values through map
        """

        mod = await self.load("tyf_stepped_mapped")
        self.assertEqual(await vars(mod)["stepped-mapped"](), (1, [2, 3]))

    async def test_stepped_delegating_generator_plain(self):
        """
        control: the same source under for-each
        """

        mod = await self.load("tyf_stepped_plain")
        self.assertEqual(await vars(mod)["stepped-plain"](), (1, [2, 3]))


# The end.
