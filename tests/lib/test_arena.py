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
Unit tests for the Arena type, the Code protect flag, and Code.pack_into
relocation

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import gc
import resource
import unittest
import weakref

from koskari.lib import Arena, Code, FormalsSpec, NamespaceSpec, OpCode

from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_lib


PAGE = resource.getpagesize()


def make_code(name, protect=True, payload=None):
    """
    Build a small Code returning a distinct payload constant
    """

    nspec = NamespaceSpec(name, (), ())
    instr = (
        [OpCode.KOS_OP_CONST, payload or f"payload-{name}"],
        [OpCode.KOS_OP_RETURN, 1],
    )
    return Code(nspec, instr, name, "<test-arena>", protect=protect)


def block_size(code):
    """
    The unrounded unified block size of a Code
    """

    return code.payload_bytes + code.padding_bytes


class TestArena(unittest.TestCase):
    """Tests for the Arena type itself"""

    def test_create_rounds_to_page(self):
        a = Arena(100)
        self.assertEqual(a.size, PAGE)
        self.assertEqual(a.used, 0)
        self.assertFalse(a.sealed)

        b = Arena(PAGE * 2 + 1)
        self.assertEqual(b.size, PAGE * 3)

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
    """Code is weakref-able and not GC-tracked"""

    def test_not_gc_tracked(self):
        code = make_code("untracked")
        self.assertFalse(gc.is_tracked(code))

    def test_weakref_support(self):
        code = make_code("weak")
        ref = weakref.ref(code)
        self.assertIs(ref(), code)
        del code
        self.assertIsNone(ref())

    def test_code_dies_by_refcount(self):
        """
        An untracked Code hanging off a function dies immediately by
        refcount when the function does
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
        # a one-page arena, and a code whose block exceeds it
        arena = Arena(8)
        nspec = NamespaceSpec("big", (), ())
        instr = [[OpCode.KOS_OP_CONST, None], [OpCode.KOS_OP_POP, 1]] * 1024
        instr.append([OpCode.KOS_OP_RETURN, 0])
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

    def test_lifetime_pinning(self):
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
