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
settings, the compiler's code allocator, and Compiler.seal()

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import resource
import unittest

import pytest

from koskari.compile import Compiler
from koskari.compile.compiler import CompilerMode
from koskari.compile.pragma.settings import compiler_pack_module
from koskari.lib import Arena, OpCode
from koskari.pragma import ModulePragma


pytestmark = pytest.mark.phase_compile


class TestPackModuleSetting(unittest.TestCase):
    """Tests for the compiler/pack-module setting function"""

    def test_defaults(self):
        self.assertIs(compiler_pack_module(), True)
        self.assertIs(compiler_pack_module(False), False)

    def test_options_rejected(self):
        with self.assertRaises(TypeError):
            compiler_pack_module(True, max_count=2)


class TestPragmaBaselines(unittest.TestCase):
    """The registered baselines for the two settings"""

    def test_baselines(self):
        pragma = ModulePragma()
        self.assertIs(pragma.get("compiler/protect-code"), False)
        self.assertIs(pragma.get("compiler/pack-module"), True)

    def test_rebuild_bare_protect(self):
        from koskari.lib import values
        pragma = ModulePragma()
        pragma = pragma.rebuild(("compiler/protect-code", values()))
        self.assertIs(pragma.get("compiler/protect-code"), True)


def mint(compiler, name, payload=None, mode=CompilerMode.FUNCTION):
    """
    Mint one Code through the compiler's complete() choke point
    """

    frag = compiler.fragment(name)
    frag.instruction(OpCode.CONST, payload or f"payload-{name}")
    frag.instruction(OpCode.RETURN, 1)
    return compiler.complete(frag, mode=mode)


def block_size(code):
    """
    The exact size of a code object's unified block
    """

    return code.payload_bytes + code.padding_bytes


def aligned(size):
    """
    The size rounded up to the arena's reservation alignment
    """

    unit = Arena.UNIT
    return ((size + unit - 1) // unit) * unit


class TestCompilerAllocator(unittest.TestCase):
    """Placement at birth through the compiler's open allocator"""

    def test_no_allocator_is_heap(self):
        comp = Compiler("sealtest", {})
        a = mint(comp, "a")

        self.assertIsNone(a.arena)
        self.assertEqual(comp.seal(), ())

    def test_function_placed_at_birth(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")
        b = mint(comp, "b")

        self.assertIsNotNone(a.arena)
        self.assertIs(a.arena, b.arena)
        self.assertFalse(a.arena.sealed)
        self.assertEqual(a.arena.size % Arena.CHUNK, 0)

    def test_expression_never_placed(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        e = mint(comp, "e", mode=CompilerMode.EXPRESSION)

        self.assertIsNone(e.arena)
        self.assertEqual(comp.seal(), ())

    def test_seal_seals_in_place(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")
        chunk = a.arena

        chunks = comp.seal()
        self.assertEqual(chunks, (chunk,))
        self.assertIs(a.arena, chunk)
        self.assertTrue(chunk.sealed)
        self.assertIsNone(comp.allocator)

    def test_seal_idempotent(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")

        first = comp.seal()
        self.assertEqual(len(first), 1)
        self.assertEqual(comp.seal(), ())
        self.assertIs(a.arena, first[0])

    def test_after_seal_is_heap(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")
        comp.seal()

        b = mint(comp, "b")
        self.assertIsNone(b.arena)
        self.assertTrue(a.arena.sealed)

    def test_reopen_after_seal(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")
        first = comp.seal()

        comp.open_allocator()
        b = mint(comp, "b")
        second = comp.seal()

        self.assertEqual(len(second), 1)
        self.assertIsNot(second[0], first[0])
        self.assertIs(b.arena, second[0])
        self.assertIs(a.arena, first[0])

    def test_open_allocator_keeps_open_one(self):
        comp = Compiler("sealtest", {})
        alloc = comp.open_allocator()
        self.assertIs(comp.open_allocator(), alloc)

    def test_pack_disabled(self):
        comp = Compiler("sealtest", {})
        comp.pack_module = False
        comp.open_allocator()
        a = mint(comp, "a")

        self.assertIsNone(a.arena)
        self.assertEqual(comp.seal(), ())

    def test_dead_newest_block_reused(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")
        chunk = a.arena

        # the dropped function was the newest block in its chunk, so its
        # bytes go back to the chunk, to its aligned start
        mint(comp, "dropped")
        self.assertEqual(chunk.used, aligned(block_size(a)))

    def test_dead_older_block_wasted(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()
        a = mint(comp, "a")
        b = mint(comp, "b")
        chunk = a.arena
        used = chunk.used

        # a is not the newest block, so its bytes stay in the chunk
        del a
        self.assertEqual(chunk.used, used)
        self.assertIs(b.arena, chunk)

    def test_many_members_fill_whole_chunks(self):
        comp = Compiler("sealtest", {})
        comp.open_allocator()

        # enough members to fill two chunks on any page size: the count
        # comes from one member's measured footprint
        probe = mint(comp, "m0")
        count = 2 * Arena.CHUNK // aligned(block_size(probe)) + 1
        codes = [probe] + [mint(comp, f"m{i}") for i in range(1, count)]

        chunks = comp.seal()
        self.assertGreater(len(chunks), 1)
        page = resource.getpagesize()
        for chunk in chunks:
            # sealed, each chunk is the whole pages its code fills
            self.assertTrue(chunk.sealed)
            self.assertLessEqual(chunk.size, Arena.CHUNK)
            self.assertEqual(chunk.size % page, 0)
            self.assertLessEqual(chunk.used, chunk.size)
            self.assertLess(chunk.size - chunk.used, page)
        for code in codes:
            self.assertIn(code.arena, chunks)

        # each member starts on the reservation alignment, so a chunk's
        # fill is at most its members' aligned sizes
        total = sum(aligned(block_size(c)) for c in codes)
        self.assertLessEqual(sum(c.used for c in chunks), total)

    def test_protect_code_setting_flows_to_code(self):
        page = resource.getpagesize()

        comp = Compiler("sealtest", {})

        comp.protect_code = False
        plain = mint(comp, "plain")
        self.assertNotEqual(plain.allocation_bytes % page, 0)

        comp.protect_code = True
        prot = mint(comp, "prot")
        self.assertEqual(prot.allocation_bytes % page, 0)

    def test_placement_outranks_protect_code(self):
        comp = Compiler("sealtest", {})
        comp.protect_code = True
        comp.open_allocator()

        a = mint(comp, "a")
        self.assertIsNotNone(a.arena)
        self.assertFalse(a.protected)
        self.assertEqual(a.allocation_bytes, block_size(a))

        e = mint(comp, "e", mode=CompilerMode.EXPRESSION)
        self.assertIsNone(e.arena)
        self.assertTrue(e.protected)


# The end.
