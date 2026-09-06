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
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_formals_star_kwd_order

Reproducer: Formals.apply mis-binds when kwd (defaults) and *star coexist.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3

Equivalent Python::

    def foobar(a, b, *more, x, y=100, **lol):
        ...

Call: foobar(1, 2, 3, 4, x=\"ecks\")

Expected: a=1, b=2, more=(3, 4), x=\"ecks\", y=100, lol={}

Bug: KosFormals_Apply / ApplyFast consumes positionals for the ``kwd`` tuple
before ``star``, so ``y`` steals a positional and ``more`` / ``x`` are wrong.
Fix when formals application consumes ``*args`` before assigning positionals
to names that are keyword-only (post-``*``) in Python terms.
"""


import unittest

import pytest

from koskari.lib import Formals, Namespace

from tests.compile.specials import SpecialsTestCase

pytestmark = pytest.mark.phase_reproducers



class TestFormalsStarBeforeKwdPositional(unittest.TestCase):
    """
    Regression target for compile/lambda FormalsSpec matching
    ``[a b *: more x y: 100 **: lol]``.
    """

    def test_apply_star_before_keyword_default_positional(self):
        """
        Positionals after required pos go to *more; y is keyword-only.

        Mirrors (foobar 1 2 3 4 x: \"ecks\") against define foobar with that
        formal list, encoded the way compile/lambda encodes it: every name
        after * is keyword-only, defaulted or not.
        """

        formals = Formals(
            ("a", "b"),
            (),
            (),
            ("x", "y"),
            "more",
            "lol",
            {"y": 100},
            None,
        )
        ns = Namespace(
            "ns",
            None,
            ("a", "b", "y", "x", "more", "lol"),
        )

        formals.apply(ns, (1, 2, 3, 4), {"x": "ecks"})

        self.assertEqual(ns["a"], 1)
        self.assertEqual(ns["b"], 2)
        self.assertEqual(ns["more"], (3, 4))
        self.assertEqual(ns["x"], "ecks")
        self.assertEqual(ns["y"], 100)
        self.assertEqual(ns["lol"], {})


class TestFormalsStarKwdOrderKoskariSource(SpecialsTestCase):
    """
    Same scenario as TestFormalsStarBeforeKwdPositional via compile/define
    and a call, matching REPL-style ``define foobar [a b *: more x y: 100 **: lol]``.
    """

    async def test_define_pre_star_default_left_to_default_binds_empty_star(self):
        """
        [a b: 1 *: rest] called short: rest is () and lands in its own
        slot; it used to be stored at the argument count, b's slot
        """
        src = """
        (begin
          (define g [a b: 1 *: rest] (col a b rest))
          #((g 1) (g 1 2) (g 1 2 3)))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        self.assertEqual(
            await expr(),
            ((1, 1, ()), (1, 2, ()), (1, 2, (3,))))


    async def test_define_pre_star_default_fills_before_star_regardless_of_names(self):
        """
        Positionals fill a defaulted pre-* name before the rest slurps,
        whatever the keyword-only names after * happen to be called
        """
        src = """
        (begin
          (define f [a z: 99 *: more c] (col a z more c))
          (define h [a b: 99 *: more x] (col a b more x))
          #((f 1 2 3 c: 0) (f 1 c: 0) (h 1 2 3 x: 0) (h 1 x: 0)))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        self.assertEqual(
            await expr(),
            ((1, 2, (3,), 0), (1, 99, (), 0),
             (1, 2, (3,), 0), (1, 99, (), 0)))


    async def test_define_foobar_call_matches_python_formals_apply(self):
        src = """
        (begin
          (define foobar [a b *: more x y: 100 **: lol]
            (col a b more x y lol))
          (foobar 1 2 3 4 x: "ecks"))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        result = await expr()
        self.assertEqual(result, (1, 2, (3, 4), "ecks", 100, {}))


    async def test_define_no_varargs_bare_kwonly_and_starstar(self):
        """
        No positionals after required slots: *more is empty; one bare kw-only x.
        """

        src = """
        (begin
          (define g [a b *: more x **: rest]
            (col a b more x rest))
          (g 1 2 x: "only"))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        result = await expr()
        self.assertEqual(result, (1, 2, (), "only", {}))


    async def test_define_override_kwonly_default(self):
        """
        Same shape as foobar but pass y explicitly so it overrides the default.
        """

        src = """
        (begin
          (define f [a b *: more x y: 100 **: lol]
            (col a b more x y lol))
          (f 1 2 3 4 x: "ecks" y: 42))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        result = await expr()
        self.assertEqual(result, (1, 2, (3, 4), "ecks", 42, {}))


    async def test_define_starstar_collects_extra_keywords(self):
        """
        Extra call keywords land in the ** binding after kw-only names are taken.
        """

        src = """
        (begin
          (define f [a b *: more x y: 100 **: lol]
            (col a b more x y lol))
          (f 1 2 3 4 x: "ecks" extra: 7))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        result = await expr()
        self.assertEqual(len(result), 6)
        self.assertEqual(result[:5], (1, 2, (3, 4), "ecks", 100))
        self.assertEqual(result[5], {"extra": 7})


    async def test_define_two_bare_kwonly_after_star(self):
        """
        Two required keyword-only names after *; no kw-only defaults in this form.
        """

        src = """
        (begin
          (define h [a *: more p q **: r]
            (col a more p q r))
          (h 10 p: 1 q: 2))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        result = await expr()
        self.assertEqual(result, (10, (), 1, 2, {}))


    async def test_define_pre_star_default_plus_kwonly_after_star(self):
        """
        Optional positional-or-keyword before *; *more; then bare kw-only x.
        """

        src = """
        (begin
          (define f [a b: 99 *: more x **: lol]
            (col a b more x lol))
          (f 1 2 3 x: "z"))
        """
        expr, mod = self.compile_expr(src, col=lambda *a: a)
        result = await expr()
        self.assertEqual(result, (1, 2, (3,), "z", {}))


# The end.
