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
End-to-end tests for module sealing: the compiler/protect-code and
compiler/pack-module pragmas driving arena packing through the module
load path

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import gc
import resource
import unittest
import weakref

import pytest

from koskari.lib import demand_call
from koskari.module import (
    detach_module_load_facets, load_module_source_async, new_module)


pytestmark = pytest.mark.phase_importer


PAGE = resource.getpagesize()

SRC = """
(define durable (lambda [x] (+ x 1)))
(define helper (lambda [y] (* y 2)))
(durable 1)
"""


async def load(source, seal=True, detach=True):
    """
    Load koskari source into a fresh module, returning the module
    """

    module = new_module("sealtest")
    vars(module)["__file__"] = "<sealtest>"
    await load_module_source_async(module, source,
                                   seal=seal, detach=detach)
    return module


class TestSealMatrix(unittest.IsolatedAsyncioTestCase):
    """The four protect-code / pack-module combinations"""

    async def test_default_off_on(self):
        """durable code lands in one sealed arena; still callable"""

        module = await load(SRC)
        ns = vars(module)
        durable = ns["durable"]
        helper = ns["helper"]

        self.assertIsNotNone(durable.code.arena)
        self.assertIs(durable.code.arena, helper.code.arena)
        self.assertTrue(durable.code.arena.sealed)
        self.assertEqual(demand_call(durable, 2), 3)
        self.assertEqual(demand_call(helper, 3), 6)

    async def test_off_off(self):
        """nothing packed, nothing page-aligned"""

        module = await load(
            "(pragma [compiler/pack-module False])" + SRC)
        code = vars(module)["durable"].code

        self.assertIsNone(code.arena)
        self.assertNotEqual(code.allocation_bytes % PAGE, 0)

    async def test_on_off(self):
        """today's per-object sealed pages"""

        module = await load(
            "(pragma [compiler/protect-code]"
            " [compiler/pack-module False])" + SRC)
        code = vars(module)["durable"].code

        self.assertIsNone(code.arena)
        self.assertEqual(code.allocation_bytes % PAGE, 0)

    async def test_on_on(self):
        """packed at seal, per-page original released"""

        module = await load(
            "(pragma [compiler/protect-code])" + SRC)
        durable = vars(module)["durable"]

        self.assertIsNotNone(durable.code.arena)
        self.assertEqual(demand_call(durable, 41), 42)


class TestSealBehavior(unittest.IsolatedAsyncioTestCase):
    """Sealing details through the load path"""

    async def test_max_count_option(self):
        """max-count: 1 gives each definition its own arena"""

        module = await load(
            "(pragma [compiler/pack-module True max-count: 1])" + SRC)
        ns = vars(module)

        a1 = ns["durable"].code.arena
        a2 = ns["helper"].code.arena
        self.assertIsNotNone(a1)
        self.assertIsNotNone(a2)
        self.assertIsNot(a1, a2)

    async def test_detach_false_seals_and_keeps_facets(self):
        """the interactive shape: sealed on completion, facets retained"""

        module = await load(SRC, detach=False)
        ns = vars(module)

        self.assertIsNotNone(ns["durable"].code.arena)
        self.assertIn("__compiler__", ns)
        self.assertIn("__reader__", ns)
        self.assertIn("__pragma__", ns)

    async def test_seal_false_does_not_pack(self):
        """seal=False leaves everything plain heap"""

        module = await load(SRC, seal=False, detach=False)
        ns = vars(module)

        self.assertIsNone(ns["durable"].code.arena)
        self.assertIn("__compiler__", ns)

    async def test_ephemeral_not_packed(self):
        """a discarded top-level expression stays out of the arena"""

        module = await load(SRC)
        durable = vars(module)["durable"]
        arena = durable.code.arena

        # only the two durable functions were placed. The arena aligns each
        # placed block's start to 32 bytes, so its instruction records stay
        # line-aligned; mirror its cumulative fill rather than a plain sum,
        # since a code's footprint need not be aligned (e.g. the appended
        # THEEND barrier can leave it off a boundary).
        def _arena_fill(codes):
            fill = 0
            for c in codes:
                fill = (fill + 31) & ~31
                fill += c.payload_bytes + c.padding_bytes
            return fill

        expected = _arena_fill((vars(module)["durable"].code,
                                vars(module)["helper"].code))
        self.assertEqual(arena.used, expected)

    async def test_nested_code_packed(self):
        """nested lambdas are collected through the same registry"""

        src = """
(define outer (lambda [x] (lambda [y] (+ x y))))
"""
        module = await load(src)
        outer = vars(module)["outer"]

        self.assertIsNotNone(outer.code.arena)
        inner = demand_call(outer, 10)
        self.assertIs(inner.code.arena, outer.code.arena)
        self.assertEqual(demand_call(inner, 32), 42)


class TestDeferredSeal(unittest.IsolatedAsyncioTestCase):
    """
    Deferred completion: several loads into one module with seal and
    detach deferred share one set of facets (one pragma, compiler, and
    reader), and one manual seal packs the union -- the shape the
    bootstrap builtins loop uses
    """

    async def multi_load(self, *sources):
        """
        Load each source into the same module deferring completion,
        then seal via the shared compiler facet and detach; returns
        (module, arenas)
        """

        module = new_module("deferred")
        ns = vars(module)
        ns["__file__"] = "<deferred>"

        for source in sources:
            await load_module_source_async(module, source,
                                           seal=False, detach=False)

        arenas = ns["__compiler__"].seal()
        detach_module_load_facets(module)
        return module, arenas

    async def test_deferred_loads_share_facets(self):
        """every load reuses the first load's pragma/compiler/reader"""

        module = new_module("deferred")
        ns = vars(module)
        ns["__file__"] = "<deferred>"

        await load_module_source_async(
            module, "(define one (lambda [] 1))",
            seal=False, detach=False)
        compiler = ns["__compiler__"]
        reader = ns["__reader__"]
        pragma = ns["__pragma__"]

        await load_module_source_async(
            module, "(define two (lambda [] 2))",
            seal=False, detach=False)

        self.assertIs(ns["__compiler__"], compiler)
        self.assertIs(ns["__reader__"], reader)
        self.assertIs(ns["__pragma__"], pragma)

        detach_module_load_facets(module)

    async def test_deferred_seal_packs_union(self):
        """codes from every load land in one arena, sealed once"""

        module, arenas = await self.multi_load(
            "(define one (lambda [] 1))",
            "(define two (lambda [] 2))")
        ns = vars(module)

        self.assertEqual(len(arenas), 1)
        self.assertIs(ns["one"].code.arena, arenas[0])
        self.assertIs(ns["two"].code.arena, arenas[0])
        self.assertTrue(arenas[0].sealed)
        self.assertEqual(demand_call(ns["one"]), 1)
        self.assertEqual(demand_call(ns["two"]), 2)
        # detach empties the facet bindings
        self.assertTrue(ns.ref("__compiler__").empty)

    async def test_no_packing_before_manual_seal(self):
        """no arena exists until the manual seal runs"""

        module = new_module("deferred")
        ns = vars(module)
        ns["__file__"] = "<deferred>"

        await load_module_source_async(
            module, "(define one (lambda [] 1))",
            seal=False, detach=False)

        self.assertIsNone(ns["one"].code.arena)

        arenas = ns["__compiler__"].seal()
        detach_module_load_facets(module)

        self.assertEqual(len(arenas), 1)
        self.assertIs(ns["one"].code.arena, arenas[0])

    async def test_pragma_persists_across_loads(self):
        """a pragma set in an earlier load governs later loads"""

        module = new_module("deferred")
        ns = vars(module)
        ns["__file__"] = "<deferred>"

        await load_module_source_async(
            module, "(pragma [compiler/pack-module False])",
            seal=False, detach=False)
        await load_module_source_async(
            module, "(define one (lambda [] 1))",
            seal=False, detach=False)

        arenas = ns["__compiler__"].seal()
        detach_module_load_facets(module)

        self.assertEqual(arenas, ())
        self.assertIsNone(ns["one"].code.arena)

    async def test_filename_tracks_each_load(self):
        """codes attribute positions to the file that defined them"""

        module = new_module("deferred")
        ns = vars(module)

        ns["__file__"] = "<part-one>"
        await load_module_source_async(
            module, "(define one (lambda [] 1))",
            seal=False, detach=False)

        ns["__file__"] = "<part-two>"
        await load_module_source_async(
            module, "(define two (lambda [] 2))",
            seal=False, detach=False)

        detach_module_load_facets(module)

        self.assertEqual(ns["one"].code.filename, "<part-one>")
        self.assertEqual(ns["two"].code.filename, "<part-two>")

    async def test_redefinition_not_packed(self):
        """
        code a later load displaced is dead by seal time and never
        packed, unlike per-load sealing which would pin it
        """

        module = new_module("deferred")
        ns = vars(module)
        ns["__file__"] = "<deferred>"

        await load_module_source_async(
            module, "(define fn (lambda [] 'first))",
            seal=False, detach=False)
        first_code = weakref.ref(ns["fn"].code)

        await load_module_source_async(
            module, "(define fn (lambda [] 'second))",
            seal=False, detach=False)

        # the displaced function lingers as cyclic garbage until a
        # collection runs; force one so this test exercises seal's
        # liveness filtering with an exactly-dead displaced definition
        gc.collect()
        self.assertIsNone(first_code(),
                          "displaced definition should be dead")

        arenas = ns["__compiler__"].seal()
        detach_module_load_facets(module)
        self.assertEqual(len(arenas), 1)

        survivor = ns["fn"].code
        self.assertIs(survivor.arena, arenas[0])
        # only the survivor was placed
        self.assertEqual(arenas[0].used,
                         survivor.payload_bytes + survivor.padding_bytes)


# The end.
