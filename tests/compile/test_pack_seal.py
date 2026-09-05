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
Unit tests for the compiler/protect-code and compiler/pack-module pragma
settings, the compiler's minted-code registry, and Compiler.seal()

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.compile import Compiler
from koskari.compile.compiler import CompilerMode
from koskari.compile.pragma import PackModule
from koskari.compile.pragma.settings import compiler_pack_module
from koskari.lib import Arena, OpCode
from koskari.pragma import ModulePragma

import pytest

pytestmark = pytest.mark.phase_compile


class TestPackModuleSetting(unittest.TestCase):
    """Tests for the compiler/pack-module setting function"""

    def test_defaults(self):
        self.assertEqual(compiler_pack_module(), PackModule(True, 0, 0))
        self.assertEqual(compiler_pack_module(False),
                         PackModule(False, 0, 0))

    def test_options_hyphen_and_underscore(self):
        opts = {"max-count": 4, "max-bytes": 4096}
        self.assertEqual(compiler_pack_module(True, **opts),
                         PackModule(True, 4, 4096))
        self.assertEqual(compiler_pack_module(True, max_count=2),
                         PackModule(True, 2, 0))
        self.assertEqual(compiler_pack_module(True, max_bytes=64),
                         PackModule(True, 0, 64))

    def test_unknown_option_rejected(self):
        with self.assertRaises(ValueError):
            compiler_pack_module(True, max_size=9)

    def test_negative_cap_rejected(self):
        with self.assertRaises(ValueError):
            compiler_pack_module(True, max_count=-1)
        with self.assertRaises(ValueError):
            compiler_pack_module(True, max_bytes=-1)


class TestPragmaBaselines(unittest.TestCase):
    """The registered baselines for the two new settings"""

    def test_baselines(self):
        pragma = ModulePragma()
        self.assertIs(pragma.get("compiler/protect-code"), False)
        self.assertEqual(pragma.get("compiler/pack-module"),
                         PackModule(True, 0, 0))

    def test_rebuild_bare_protect(self):
        from koskari.lib import values
        pragma = ModulePragma()
        pragma = pragma.rebuild(("compiler/protect-code", values()))
        self.assertIs(pragma.get("compiler/protect-code"), True)


def mint(compiler, name, payload=None):
    """
    Mint one Code through the compiler's complete() choke point
    """

    frag = compiler.fragment(name)
    frag.instruction(OpCode.CONST, payload or f"payload-{name}")
    frag.instruction(OpCode.RETURN, 1)
    return compiler.complete(frag, mode=CompilerMode.EXPRESSION)


class TestCompilerSeal(unittest.TestCase):
    """Tests for the minted registry and Compiler.seal()"""

    def test_seal_packs_survivors(self):
        comp = Compiler("sealtest", {})
        a = mint(comp, "a")
        b = mint(comp, "b")

        arenas = comp.seal()
        self.assertEqual(len(arenas), 1)
        self.assertIs(a.arena, arenas[0])
        self.assertIs(b.arena, arenas[0])
        self.assertTrue(arenas[0].sealed)

    def test_ephemeral_drops_off(self):
        comp = Compiler("sealtest", {})
        a = mint(comp, "a")
        mint(comp, "ephemeral")

        arenas = comp.seal()
        self.assertEqual(len(arenas), 1)
        self.assertIs(a.arena, arenas[0])
        # only the survivor was placed
        self.assertEqual(arenas[0].used,
                         a.payload_bytes + a.padding_bytes)

    def test_seal_idempotent(self):
        comp = Compiler("sealtest", {})
        a = mint(comp, "a")

        first = comp.seal()
        self.assertEqual(len(first), 1)
        self.assertEqual(comp.seal(), ())
        self.assertIs(a.arena, first[0])

    def test_seal_disabled(self):
        comp = Compiler("sealtest", {})
        comp.pack_module = PackModule(False, 0, 0)
        a = mint(comp, "a")

        self.assertEqual(comp.seal(), ())
        self.assertIsNone(a.arena)
        # registry drained even when disabled
        comp.pack_module = PackModule(True, 0, 0)
        self.assertEqual(comp.seal(), ())

    def test_seal_nothing_survived(self):
        comp = Compiler("sealtest", {})
        mint(comp, "ephemeral")
        self.assertEqual(comp.seal(), ())

    def test_max_count_partitions_in_order(self):
        comp = Compiler("sealtest", {})
        comp.pack_module = PackModule(True, 2, 0)
        codes = [mint(comp, f"c{i}") for i in range(5)]

        arenas = comp.seal()
        self.assertEqual(len(arenas), 3)
        self.assertIs(codes[0].arena, arenas[0])
        self.assertIs(codes[1].arena, arenas[0])
        self.assertIs(codes[2].arena, arenas[1])
        self.assertIs(codes[3].arena, arenas[1])
        self.assertIs(codes[4].arena, arenas[2])

    def test_many_members_fit_their_arena(self):
        # every member starts on the arena's reservation unit, so a
        # segment of many members whose sizes are not multiples of it
        # needs more than the sum of their sizes; the planner counts
        # that, and the page-rounding slack is not relied on
        comp = Compiler("sealtest", {})
        codes = [mint(comp, f"m{i}") for i in range(60)]
        size = codes[0].payload_bytes + codes[0].padding_bytes
        self.assertNotEqual(size % Arena.UNIT, 0)

        arenas = comp.seal()
        self.assertEqual(len(arenas), 1)
        for code in codes:
            self.assertIs(code.arena, arenas[0])
        self.assertLessEqual(arenas[0].used, arenas[0].size)

    def test_max_bytes_soft_cap(self):
        comp = Compiler("sealtest", {})
        a = mint(comp, "a")
        b = mint(comp, "b")
        size_a = a.payload_bytes + a.padding_bytes

        # cap below a single member: each lands in a singleton arena
        # rather than failing to place
        comp.pack_module = PackModule(True, 0, size_a - 1)
        arenas = comp.seal()
        self.assertEqual(len(arenas), 2)
        self.assertIsNot(a.arena, b.arena)

    def test_protect_code_setting_flows_to_code(self):
        import resource
        page = resource.getpagesize()

        comp = Compiler("sealtest", {})
        comp.pack_module = PackModule(False, 0, 0)

        comp.protect_code = False
        plain = mint(comp, "plain")
        self.assertNotEqual(plain.allocation_bytes % page, 0)

        comp.protect_code = True
        prot = mint(comp, "prot")
        self.assertEqual(prot.allocation_bytes % page, 0)

    def test_already_packed_skipped(self):
        from koskari.lib import Arena

        comp = Compiler("sealtest", {})
        a = mint(comp, "a")
        b = mint(comp, "b")

        solo = Arena(a.payload_bytes + a.padding_bytes)
        a.pack_into(solo)
        solo.seal()

        arenas = comp.seal()
        self.assertEqual(len(arenas), 1)
        self.assertIs(a.arena, solo)
        self.assertIs(b.arena, arenas[0])


class TestSealAccumulation(unittest.TestCase):
    """A long-lived compiler seals repeatedly, packing increments"""

    def test_repeated_seal_packs_increments(self):
        comp = Compiler("repl", {})
        a = mint(comp, "a")

        first = comp.seal()
        self.assertEqual(len(first), 1)
        self.assertIs(a.arena, first[0])

        # definitions accumulated since the last seal pack into a
        # fresh arena; already-packed code is untouched
        b = mint(comp, "b")
        second = comp.seal()
        self.assertEqual(len(second), 1)
        self.assertIsNot(second[0], first[0])
        self.assertIs(b.arena, second[0])
        self.assertIs(a.arena, first[0])


# The end.
