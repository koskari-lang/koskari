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
Unit tests for the Arena type, the CodeAllocator type, the Code protect
flag, and Code.pack_into relocation

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import gc
import resource
import unittest
import weakref

import pytest

from koskari.lib import (
    Arena, Code, CodeAllocator, FormalsSpec, NamespaceSpec, OpCode)
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


PAGE = resource.getpagesize()


def make_code(name, protect=True, payload=None, allocator=None):
    """
    Build a small Code returning a distinct payload constant
    """

    nspec = NamespaceSpec(name, (), ())
    instr = (
        [OpCode.CONST, payload or f"payload-{name}"],
        [OpCode.RETURN, 1],
    )
    return Code(nspec, instr, name, "<test-arena>", protect=protect,
                allocator=allocator)


def make_sized(name, pairs, allocator=None):
    """
    Build a Code of pairs CONST/POP instruction pairs, for a block whose
    size grows with pairs
    """

    nspec = NamespaceSpec(name, (), ())
    instr = [[OpCode.CONST, None], [OpCode.POP, 1]] * pairs
    instr.append([OpCode.RETURN, 0])
    return Code(nspec, instr, name, "<test-arena>", allocator=allocator)


def block_size(code):
    """
    The unrounded unified block size of a Code
    """

    return code.payload_bytes + code.padding_bytes


def aligned(size):
    """
    The size rounded up to the arena's reservation alignment
    """

    unit = Arena.UNIT
    return ((size + unit - 1) // unit) * unit


class TestArena(unittest.TestCase):
    """Tests for the Arena type itself"""

    def test_chunk_is_whole_pages(self):
        self.assertGreaterEqual(Arena.CHUNK, PAGE)
        self.assertEqual(Arena.CHUNK % PAGE, 0)

    def test_create_rounds_to_chunk(self):
        a = Arena(100)
        self.assertEqual(a.size, Arena.CHUNK)
        self.assertEqual(a.used, 0)
        self.assertFalse(a.sealed)

        b = Arena(Arena.CHUNK * 2 + 1)
        self.assertEqual(b.size, Arena.CHUNK * 3)

    def test_create_rejects_nonpositive(self):
        with self.assertRaises(ValueError):
            Arena(0)
        with self.assertRaises(ValueError):
            Arena(-5)

    def test_seal_and_double_seal(self):
        a = Arena(64)
        a.seal()
        self.assertTrue(a.sealed)
        with self.assertRaises(ValueError):
            a.seal()

    def test_sealed_arena_deallocates(self):
        a = Arena(64)
        a.seal()
        ref = weakref.ref(a)
        del a
        self.assertIsNone(ref())

    def test_repr(self):
        a = Arena(64)
        self.assertIn("Arena", repr(a))
        a.seal()
        self.assertIn("sealed", repr(a))


class TestCodeAllocator(unittest.TestCase):
    """Tests for CodeAllocator placement at birth"""

    def test_new_allocator_is_open_and_empty(self):
        alloc = CodeAllocator()
        self.assertFalse(alloc.sealed)
        self.assertEqual(alloc.chunks, ())
        self.assertIn("CodeAllocator", repr(alloc))

    def test_code_placed_at_birth(self):
        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)

        self.assertEqual(alloc.chunks, (a.arena,))
        self.assertFalse(a.arena.sealed)
        self.assertFalse(a.protected)
        self.assertEqual(a.allocation_bytes, block_size(a))
        self.assertEqual(a.arena.used, block_size(a))

    def test_placed_matches_heap(self):
        alloc = CodeAllocator()
        placed = make_code("a", allocator=alloc)
        plain = make_code("a", protect=False)
        self.assertEqual(placed.soften(), plain.soften())
        self.assertEqual(placed.constants_pool, plain.constants_pool)

    def test_none_is_heap(self):
        a = make_code("a", allocator=None)
        self.assertIsNone(a.arena)

    def test_non_allocator_rejected(self):
        with self.assertRaises(TypeError):
            make_code("a", allocator=object())
        with self.assertRaises(TypeError):
            make_code("a", allocator=Arena(64))

    def test_members_share_a_chunk(self):
        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)
        b = make_code("b", allocator=alloc)

        self.assertIs(a.arena, b.arena)
        self.assertEqual(a.arena.used,
                         aligned(block_size(a)) + block_size(b))

    def test_first_fit_returns_to_earlier_chunk(self):
        alloc = CodeAllocator()
        scale = Arena.CHUNK // 4096

        # about three quarters of a chunk, then half of one: the second
        # cannot follow the first, so it opens a second chunk
        first = make_sized("first", 44 * scale, allocator=alloc)
        second = make_sized("second", 30 * scale, allocator=alloc)
        tail = first.arena.size - aligned(first.arena.used)
        self.assertGreater(block_size(second), tail)
        self.assertIsNot(second.arena, first.arena)

        # a small one fits behind the first, so it goes there
        small = make_code("small", allocator=alloc)
        self.assertLessEqual(block_size(small), tail)
        self.assertIs(small.arena, first.arena)
        self.assertEqual(alloc.chunks, (first.arena, second.arena))

    def test_oversize_gets_whole_chunks(self):
        alloc = CodeAllocator()
        big = make_sized("big", Arena.CHUNK // 16, allocator=alloc)

        self.assertGreater(block_size(big), Arena.CHUNK)
        self.assertEqual(big.arena.size % Arena.CHUNK, 0)
        self.assertGreaterEqual(big.arena.size, block_size(big))
        self.assertLess(big.arena.size - block_size(big), Arena.CHUNK)

    def test_dead_newest_returns_its_bytes(self):
        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)
        chunk = a.arena
        used = chunk.used

        b = make_code("b", allocator=alloc)
        self.assertGreater(chunk.used, used)

        # the fill returns to b's start, the aligned end of a
        del b
        self.assertEqual(chunk.used, aligned(used))

        # and the next block takes the space back
        c = make_code("c", allocator=alloc)
        self.assertEqual(chunk.used, aligned(used) + block_size(c))

    def test_dead_older_keeps_its_bytes(self):
        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)
        b = make_code("b", allocator=alloc)
        chunk = b.arena
        used = chunk.used

        del a
        self.assertEqual(chunk.used, used)

    def test_seal(self):
        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)
        chunk = a.arena

        chunks = alloc.seal()
        self.assertEqual(chunks, (chunk,))
        self.assertTrue(chunk.sealed)
        self.assertTrue(alloc.sealed)
        self.assertEqual(alloc.chunks, ())
        self.assertIn("sealed", repr(alloc))
        self.assertIs(a.arena, chunk)

    def test_seal_twice_rejected(self):
        alloc = CodeAllocator()
        alloc.seal()
        with self.assertRaises(ValueError):
            alloc.seal()

    def test_reserve_after_seal_rejected(self):
        alloc = CodeAllocator()
        alloc.seal()
        with self.assertRaises(ValueError):
            make_code("a", allocator=alloc)

    def test_no_return_after_seal(self):
        alloc = CodeAllocator()
        make_code("a", allocator=alloc)
        b = make_code("b", allocator=alloc)
        chunk = b.arena
        alloc.seal()
        used = chunk.used

        del b
        self.assertEqual(chunk.used, used)

    def test_chunk_lives_until_last_member_dies(self):
        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)
        b = make_code("b", allocator=alloc)
        alloc.seal()

        ref = weakref.ref(a.arena)
        del alloc
        self.assertIsNotNone(ref())
        del a
        self.assertIsNotNone(ref())
        del b
        self.assertIsNone(ref())

    def test_unsealed_allocator_dropped(self):
        """
        Chunks of an allocator dropped unsealed stay alive, writable,
        through their members
        """

        alloc = CodeAllocator()
        a = make_code("a", allocator=alloc)
        del alloc
        self.assertFalse(a.arena.sealed)
        self.assertEqual(a.soften(), make_code("a").soften())

    def test_placed_code_executes_across_seal(self):
        alloc = CodeAllocator()
        nspec = NamespaceSpec(None, (), ())
        instr = [
            [OpCode.CONST, 1],
            [OpCode.YIELD, 1],
            [OpCode.CONST, 2],
            [OpCode.YIELD, 1],
            [OpCode.RETURN, 0],
        ]
        code = Code(nspec, instr, "gen", "<test-arena>", allocator=alloc)
        ns = nspec.get_namespace(None)

        it = iter(make_callable(code, ns)())
        self.assertEqual(next(it), 1)

        alloc.seal()
        self.assertTrue(code.arena.sealed)
        self.assertEqual(next(it), 2)


class TestCodeProtect(unittest.TestCase):
    """Tests for the Code constructor protect flag"""

    def test_default_is_page_protected(self):
        code = make_code("prot")
        self.assertEqual(code.allocation_bytes % PAGE, 0)

    def test_unprotected_is_exact(self):
        code = make_code("plain", protect=False)
        self.assertLess(code.allocation_bytes, PAGE)
        self.assertEqual(code.allocation_bytes, block_size(code))
        self.assertEqual(code.page_rounding_bytes, 0)

    def test_both_modes_equivalent(self):
        prot = make_code("a")
        plain = make_code("a", protect=False)
        self.assertEqual(prot.soften(), plain.soften())
        self.assertEqual(prot.constants_pool, plain.constants_pool)


class TestCodeGC(unittest.TestCase):
    """Code is weakref-able and GC-tracked for its cache slots"""

    def test_gc_tracked(self):
        code = make_code("tracked")
        self.assertTrue(gc.is_tracked(code))

    def test_weakref_support(self):
        code = make_code("weak")
        ref = weakref.ref(code)
        self.assertIs(ref(), code)
        del code
        self.assertIsNone(ref())

    def test_code_dies_by_refcount(self):
        """
        A Code hanging off a function dies immediately by refcount when
        the function does, without waiting for a collection
        """

        code, ns = build_expr([["CONST", None], ["RETURN", 1]])
        fn = make_callable(code, ns, FormalsSpec((), ()))

        code_ref = weakref.ref(code)
        del code
        self.assertIsNotNone(code_ref())
        del fn
        self.assertIsNone(code_ref())


class TestPackInto(unittest.TestCase):
    """Tests for Code.pack_into relocation"""

    def test_pack_two_codes(self):
        a = make_code("a")
        b = make_code("b", protect=False)

        before_a = a.soften()
        before_b = b.soften()
        const_a = a.constant(a.oparg(0)[1])

        arena = Arena(block_size(a) + block_size(b) + 16)
        a.pack_into(arena)
        b.pack_into(arena)
        arena.seal()

        self.assertIs(a.arena, arena)
        self.assertIs(b.arena, arena)
        self.assertGreaterEqual(arena.used, block_size(a) + block_size(b))

        # relocation moved storage, never references or contents
        self.assertEqual(a.soften(), before_a)
        self.assertEqual(b.soften(), before_b)
        self.assertIs(a.constant(a.oparg(0)[1]), const_a)

        # allocation now reports the member footprint
        self.assertEqual(a.allocation_bytes, block_size(a))

    def test_double_pack_rejected(self):
        a = make_code("a")
        arena = Arena(block_size(a) * 2)
        a.pack_into(arena)
        with self.assertRaises(ValueError):
            a.pack_into(arena)

    def test_sealed_arena_rejected(self):
        c = make_code("c")
        arena = Arena(block_size(c))
        arena.seal()
        with self.assertRaises(ValueError):
            c.pack_into(arena)
        self.assertIsNone(c.arena)

    def test_capacity_exceeded_rejected(self):
        # a one-page arena, and a code whose block exceeds it. 1024
        # CONST/POP pairs overflow a 4 KiB page but fit under a 16 KiB
        # page (Apple Silicon), so scale the body with the host page size.
        arena = Arena(8)
        nspec = NamespaceSpec("big", (), ())
        pairs = max(1024, PAGE // 4)
        instr = [[OpCode.CONST, None], [OpCode.POP, 1]] * pairs
        instr.append([OpCode.RETURN, 0])
        big = Code(nspec, instr, "big", "<test-arena>")

        self.assertGreater(block_size(big), arena.size)
        with self.assertRaises(ValueError):
            big.pack_into(arena)
        self.assertIsNone(big.arena)
        self.assertEqual(arena.used, 0)

    def test_non_arena_rejected(self):
        c = make_code("c")
        with self.assertRaises(TypeError):
            c.pack_into(object())

    def test_arena_lives_until_last_member_dies(self):
        """
        The arena lives until the last packed member dies
        """

        a = make_code("a")
        b = make_code("b")
        arena = Arena(block_size(a) + block_size(b) + 16)
        a.pack_into(arena)
        b.pack_into(arena)
        arena.seal()

        ref = weakref.ref(arena)
        del arena
        self.assertIsNotNone(ref())
        del a
        self.assertIsNotNone(ref())
        del b
        self.assertIsNone(ref())

    def test_packed_code_executes(self):
        code, ns = build_expr([["CONST", "ran"], ["RETURN", 1]])
        arena = Arena(block_size(code))
        code.pack_into(arena)
        arena.seal()

        expr = make_callable(code, ns)
        self.assertEqual(expr().demand(), "ran")


class TestPackSuspended(unittest.TestCase):
    """Relocation is safe across suspended frames"""

    def test_pack_between_yields(self):
        """
        A generator suspended mid-yield resumes correctly after its
        code is packed: the frame re-derives the relocated buffer
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen = expr()

        it = iter(agen)
        self.assertEqual(next(it), 1)

        arena = Arena(block_size(code))
        code.pack_into(arena)
        arena.seal()
        self.assertIs(code.arena, arena)

        self.assertEqual(next(it), 2)
        self.assertEqual(next(it), 3)


# The end.
