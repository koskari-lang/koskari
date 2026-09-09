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
tests.reproducers.test_provider_drive_matrix

The full drive matrix for the native map and filter providers: every
combination of function kind (python callable, koskari function, and
the None predicate for filter), source kind (plain sequence, koskari
generator, koskari demand generator, python generator, python async
generator), and drive (python async for, python demand iteration, a
koskari for-each consumer, and a koskari consumer that demand-drives
through list), plus construction of the providers from koskari itself

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import KosFilter, KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define inc [x]:
    (+ x 1)

define odd? [x]:
    (== 1 (% x 2))

define gen-src []:
    yield 1
    yield 2
    yield 3
    yield 4

define consume [it]:
    define total 0
    for-each [v it]:
        set! total (+ total v)
    total

define consume-demand [it]:
    (consume (list it))

define make-map [f seq]:
    (map f seq)

define make-filter [p seq]:
    (filter p seq)
"""


def py_gen():
    yield 1
    yield 2
    yield 3
    yield 4


async def py_async_gen():
    yield 1
    yield 2
    yield 3
    yield 4


class TestProviderDriveMatrix(unittest.IsolatedAsyncioTestCase):
    """
    Every function x source x drive combination for map and filter
    """

    async def asyncSetUp(self):
        module = new_module("provider_matrix")
        vars(module)["__file__"] = "<provider_matrix>"
        await load_module_source_async(module, SRC)
        self.v = vars(module)

    def sources(self):
        v = self.v
        return {
            "sequence": lambda: [1, 2, 3, 4],
            "kos-gen": lambda: v["gen-src"](),
            "kos-demand-gen": lambda: iter(v["gen-src"]()),
            "py-gen": py_gen,
            "py-async-gen": py_async_gen,
        }

    async def drive(self, drive_name, provider):
        if drive_name == "py-async":
            return [x async for x in provider]
        if drive_name == "py-demand":
            return list(provider)
        if drive_name == "kos-ambient":
            return await self.v["consume"](provider)
        if drive_name == "kos-demand":
            return await self.v["consume-demand"](provider)
        raise ValueError(drive_name)

    async def check(self, make_provider, expected_list):
        expected_sum = sum(expected_list)
        for src_name, src in self.sources().items():
            for drive_name in ("py-async", "py-demand",
                               "kos-ambient", "kos-demand"):
                with self.subTest(source=src_name, drive=drive_name):
                    result = await self.drive(
                        drive_name, make_provider(src()))
                    if drive_name in ("py-async", "py-demand"):
                        self.assertEqual(result, expected_list)
                    else:
                        self.assertEqual(result, expected_sum)

    async def test_map_python_function(self):
        await self.check(
            lambda src: KosMap(lambda x: x + 1, src), [2, 3, 4, 5])

    async def test_map_koskari_function(self):
        await self.check(
            lambda src: KosMap(self.v["inc"], src), [2, 3, 4, 5])

    async def test_filter_python_predicate(self):
        await self.check(
            lambda src: KosFilter(lambda x: x % 2, src), [1, 3])

    async def test_filter_koskari_predicate(self):
        await self.check(
            lambda src: KosFilter(self.v["odd?"], src), [1, 3])

    async def test_filter_none_predicate(self):
        await self.check(
            lambda src: KosFilter(None, src), [1, 2, 3, 4])

    async def test_koskari_construction(self):
        """
        (map f seq) and (filter p seq) built inside koskari over every
        source kind, drained through the python async surface
        """
        v = self.v
        for src_name, src in self.sources().items():
            with self.subTest(provider="map", source=src_name):
                m = await v["make-map"](v["inc"], src())
                self.assertEqual([x async for x in m], [2, 3, 4, 5])
            with self.subTest(provider="filter", source=src_name):
                f = await v["make-filter"](v["odd?"], src())
                self.assertEqual([x async for x in f], [1, 3])


# The end.
