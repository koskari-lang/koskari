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
Unit tests for the gensym type: a symbol scoped by expansion marks

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import gc
import unittest

import pytest

from koskari.lib import cons, gensym, keyword, kos_repr, nil, symbol


pytestmark = pytest.mark.phase_lib


class TestGensymType(unittest.TestCase):


    def test_is_a_symbol(self):
        g = gensym(symbol("tmp"), marks=1)
        self.assertIsInstance(g, symbol)
        self.assertIsInstance(g, gensym)
        self.assertIs(type(g), gensym)


    def test_fresh_is_a_gensym(self):
        g = gensym("tmp")
        self.assertIs(type(g), gensym)
        self.assertIs(g.base, symbol("tmp"))
        self.assertEqual(len(g.marks), 1)
        self.assertEqual(str(g), f"tmp#{g.marks[0]:x}")


    def test_derived_spelling(self):
        g = gensym(symbol("tmp"), marks=1)
        self.assertIs(g.base, symbol("tmp"))
        self.assertEqual(g.marks, (1,))
        self.assertEqual(str(g), "tmp#1")
        self.assertEqual(repr(g), "<gensym 'tmp#1'>")


    def test_marks_tuple_in_order_hex(self):
        g = gensym(symbol("y"), marks=(1, 255))
        self.assertEqual(g.marks, (1, 255))
        self.assertEqual(str(g), "y#1#ff")


    def test_marks_accepts_list(self):
        g = gensym(symbol("y"), marks=[3, 4])
        self.assertEqual(g.marks, (3, 4))


    def test_base_accepts_str(self):
        g = gensym("y", marks=5)
        self.assertIs(g.base, symbol("y"))
        self.assertEqual(str(g), "y#5")


    def test_is_not_a_base_type(self):
        with self.assertRaises(TypeError):
            type("SubGensym", (gensym,), {})


    def test_rejects_non_symbol_base(self):
        with self.assertRaises(TypeError):
            gensym(keyword("tmp"), marks=1)
        with self.assertRaises(TypeError):
            gensym(gensym(symbol("tmp"), marks=1), marks=2)


    def test_rejects_empty_marks(self):
        with self.assertRaises(ValueError):
            gensym(symbol("tmp"), marks=())


    def test_rejects_non_int_marks(self):
        with self.assertRaises(TypeError):
            gensym(symbol("tmp"), marks=("a",))


    def test_rejects_predicate_with_marks(self):
        with self.assertRaises(TypeError):
            gensym(symbol("tmp"), lambda s: True, marks=1)


    def test_rejects_marks_without_base(self):
        with self.assertRaises(TypeError):
            gensym(marks=1)


    def test_base_of_a_plain_atom_is_itself(self):
        self.assertIs(symbol("tmp").base, symbol("tmp"))
        self.assertIs(keyword("tmp").base, keyword("tmp"))


class TestGensymIdentity(unittest.TestCase):


    def test_one_object_per_base_and_marks(self):
        a = gensym(symbol("t"), marks=(1,))
        b = gensym(symbol("t"), marks=(1,))
        self.assertIs(a, b)


    def test_symbol_of_the_spelling_is_the_gensym(self):
        a = gensym(symbol("t"), marks=(1,))
        self.assertIs(symbol("t#1"), a)


    def test_equality_is_identity(self):
        a = gensym(symbol("t"), marks=(1,))
        c = gensym(symbol("t"), marks=(2,))
        d = gensym(symbol("u"), marks=(1,))
        self.assertEqual(a, a)
        self.assertNotEqual(a, c)
        self.assertNotEqual(a, d)
        self.assertNotEqual(a, symbol("t"))
        self.assertNotEqual(symbol("t"), a)


    def test_distinct_from_base_as_a_key(self):
        a = gensym(symbol("t"), marks=(1,))
        d = {symbol("t"): 1, a: 2}
        self.assertEqual(len(d), 2)
        self.assertEqual(d[symbol("t")], 1)
        self.assertEqual(d[a], 2)
        self.assertNotIn(a, (symbol("t"),))
        self.assertNotIn(symbol("t"), (a,))


    def test_spelling_held_by_a_plain_symbol_raises(self):
        held = symbol("held#7")
        with self.assertRaises(ValueError):
            gensym(symbol("held"), marks=7)
        del held


    def test_spelling_held_by_another_gensym_raises(self):
        held = gensym(symbol("a#1"), marks=2)
        self.assertEqual(str(held), "a#1#2")
        with self.assertRaises(ValueError):
            gensym(symbol("a"), marks=(1, 2))
        del held


    def test_fresh_skips_a_held_spelling(self):
        nxt = gensym.next_mark()
        held = symbol(f"skip#{nxt + 1:x}")
        g = gensym("skip")
        self.assertIsNot(g, held)
        self.assertEqual(g.marks[0], nxt + 2)
        del held


    def test_fresh_skips_an_explicit_gensym(self):
        nxt = gensym.next_mark()
        held = gensym(symbol("q"), marks=(nxt + 1,))
        g = gensym("q")
        self.assertIsNot(g, held)
        self.assertEqual(g.marks[0], nxt + 2)
        del held


    def test_sixteen_digit_mark(self):
        top = gensym(symbol("x"), marks=(2 ** 64 - 1,))
        self.assertEqual(str(top), "x#ffffffffffffffff")
        below = gensym(symbol("x"), marks=(2 ** 64 - 2,))
        self.assertIsNot(below, top)
        self.assertEqual(str(below), "x#fffffffffffffffe")


    def test_fresh_extends_a_gensym(self):
        outer = gensym(symbol("t"), marks=(1,))
        inner = gensym(outer)
        self.assertIs(inner.base, symbol("t"))
        self.assertEqual(inner.marks[:1], (1,))
        self.assertEqual(len(inner.marks), 2)
        self.assertTrue(str(inner).startswith("t#1#"))


    def test_next_mark_advances(self):
        a = gensym.next_mark()
        b = gensym.next_mark()
        self.assertEqual(b, a + 1)
        g = gensym("m")
        self.assertEqual(g.marks[0], b + 1)


    def test_two_processes_draw_different_bases(self):
        """
        Marks count up from a base drawn once per process, so two
        processes do not spell the same key for different names
        """

        import subprocess
        import sys

        code = "from koskari.lib import gensym; print(gensym.next_mark())"
        bases = set()
        for _ in range(2):
            out = subprocess.run(
                [sys.executable, "-c", code], capture_output=True,
                text=True, check=True).stdout.strip()
            bases.add(int(out))
        self.assertEqual(len(bases), 2)
        self.assertTrue(all(b >= 1 for b in bases))


class TestGensymLifetime(unittest.TestCase):


    def test_dealloc_leaves_the_base_interned(self):
        base = symbol("keep")
        g = gensym(base, marks=(9,))
        self.assertIs(symbol("keep#9"), g)
        del g
        gc.collect()
        self.assertIs(symbol("keep"), base)
        fresh = symbol("keep#9")
        self.assertIs(type(fresh), symbol)


    def test_rejected_candidate_releases_its_spelling(self):
        seen = []

        def predicate(sym):
            seen.append(str(sym))
            return len(seen) >= 2

        g = gensym("cand", predicate)
        self.assertEqual(len(seen), 2)
        self.assertEqual(str(g), seen[1])
        rejected = symbol(seen[0])
        self.assertIs(type(rejected), symbol)


class TestGensymPrinting(unittest.TestCase):


    def test_kos_repr_is_the_base(self):
        g = gensym(symbol("t"), marks=1)
        self.assertEqual(kos_repr(g), "t")
        self.assertEqual(str(g), "t#1")


    def test_printed_form_shows_the_base(self):
        form = cons(gensym(symbol("let"), marks=1),
                    gensym(symbol("t"), marks=1), 1, nil)
        self.assertEqual(str(form), "(let t 1)")
        self.assertEqual(kos_repr(form), "(let t 1)")
        dotted = cons(1, gensym(symbol("t"), marks=1))
        self.assertEqual(str(dotted), "(1 . t)")


# The end.
