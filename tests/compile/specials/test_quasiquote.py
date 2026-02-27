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
Tests for koskari.compile compile/quasiquote special form, covering:
- atom quasi-quote (symbols, keywords, numbers)
- proper list quasi-quote (all-constant tuple-optimisation path)
- unquote (,) in various positions
- unquote-splicing (,@) single and multiple splice sites
- dotted pair with unquote tail
- nested quasiquote (level > 0 keeps inner unquote as data)
- unquote and unquote-splicing outside quasiquote raise SpecialError

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""

import unittest

from koskari.exceptions import SpecialError
from koskari.lib import cons, nil, symbol

from . import SpecialsTestCase


class TestQuasiquoteAtoms(SpecialsTestCase):
    """
    Quasi-quoting atomic values
    """

    def test_quasiquote_symbol(self):
        """
        `foo yields the symbol foo
        """

        expr, mod = self.compile_expr("`foo")
        result = expr().run()
        self.assertEqual(result, symbol("foo"))


    def test_quasiquote_number(self):
        """
        `42 yields the integer 42
        """

        expr, mod = self.compile_expr("`42")
        result = expr().run()
        self.assertEqual(result, 42)


    def test_quasiquote_nil(self):
        """
        `() yields nil
        """

        expr, mod = self.compile_expr("`()")
        result = expr().run()
        self.assertIs(result, nil)


class TestQuasiquoteProperList(SpecialsTestCase):
    """
    Quasi-quoting proper lists with no unquote (all-constant tuple path)
    """

    def test_quasiquote_simple_list(self):
        """
        `(a b c) yields (a b c) as a proper list
        """

        expr, mod = self.compile_expr("`(a b c)")
        result = expr().run()
        self.assertEqual(
            [symbol("a"), symbol("b"), symbol("c")],
            list(result.unpack()))


    def test_quasiquote_nested_list(self):
        """
        `(a (b c)) yields a nested proper list
        """

        expr, mod = self.compile_expr("`(a (b c))")
        result = expr().run()
        outer = list(result.unpack())
        self.assertEqual(symbol("a"), outer[0])
        self.assertEqual(
            [symbol("b"), symbol("c")],
            list(outer[1].unpack()))


    def test_quasiquote_list_is_proper(self):
        """
        `(a b) produces a proper list
        """

        expr, mod = self.compile_expr("`(a b)")
        result = expr().run()
        self.assertTrue(result.is_proper())


class TestQuasiquoteUnquote(SpecialsTestCase):
    """
    Quasi-quoting lists containing unquote (,expr)
    """

    def test_unquote_single_element(self):
        """
        `(,x) with x=99 yields (99)
        """

        expr, mod = self.compile_expr("`(,x)", x=99)
        result = expr().run()
        self.assertEqual([99], list(result.unpack()))


    def test_unquote_head_of_list(self):
        """
        `(,x b c) evaluates x and keeps b, c as symbols
        """

        expr, mod = self.compile_expr("`(,x b c)", x=1)
        result = expr().run()
        self.assertEqual(
            [1, symbol("b"), symbol("c")],
            list(result.unpack()))


    def test_unquote_middle_of_list(self):
        """
        `(a ,x c) evaluates x in the middle position
        """

        expr, mod = self.compile_expr("`(a ,x c)", x=2)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), 2, symbol("c")],
            list(result.unpack()))


    def test_unquote_tail_of_list(self):
        """
        `(a b ,x) evaluates x as last element
        """

        expr, mod = self.compile_expr("`(a b ,x)", x=3)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), symbol("b"), 3],
            list(result.unpack()))


    def test_unquote_expression(self):
        """
        `(a ,(+ x 1)) evaluates the sub-expression at runtime
        """

        expr, mod = self.compile_expr("`(a ,(+ x 1))", x=10)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), 11],
            list(result.unpack()))


class TestQuasiquoteSplice(SpecialsTestCase):
    """
    Quasi-quoting lists containing unquote-splicing (,@expr)
    """

    def _make_list(self, *vals):
        result = nil
        for v in reversed(vals):
            result = cons(v, result)
        return result


    def test_splice_only(self):
        """
        `(,@lst) with lst=(1 2 3) yields (1 2 3)
        """

        lst = self._make_list(1, 2, 3)
        expr, mod = self.compile_expr("`(,@lst)", lst=lst)
        result = expr().run()
        self.assertEqual([1, 2, 3], list(result.unpack()))


    def test_splice_at_front(self):
        """
        `(,@lst b c) prepends lst before b, c
        """

        lst = self._make_list(1, 2)
        expr, mod = self.compile_expr("`(,@lst b c)", lst=lst)
        result = expr().run()
        self.assertEqual(
            [1, 2, symbol("b"), symbol("c")],
            list(result.unpack()))


    def test_splice_in_middle(self):
        """
        `(a ,@lst c) inserts lst after a
        """

        lst = self._make_list(10, 20)
        expr, mod = self.compile_expr("`(a ,@lst c)", lst=lst)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), 10, 20, symbol("c")],
            list(result.unpack()))


    def test_splice_at_end(self):
        """
        `(a b ,@lst) appends lst after a, b
        """

        lst = self._make_list(3, 4)
        expr, mod = self.compile_expr("`(a b ,@lst)", lst=lst)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), symbol("b"), 3, 4],
            list(result.unpack()))


    def test_splice_two_sites(self):
        """
        `(,@x ,@y) with x=(1 2) and y=(3 4) yields (1 2 3 4)
        """

        x = self._make_list(1, 2)
        y = self._make_list(3, 4)
        expr, mod = self.compile_expr("`(,@x ,@y)", x=x, y=y)
        result = expr().run()
        self.assertEqual([1, 2, 3, 4], list(result.unpack()))


    def test_splice_with_unquote(self):
        """
        `(a ,v ,@lst b) mixes unquote and splice
        """

        lst = self._make_list(10, 20)
        expr, mod = self.compile_expr("`(a ,v ,@lst b)", v=99, lst=lst)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), 99, 10, 20, symbol("b")],
            list(result.unpack()))


    def test_splice_empty_list(self):
        """
        `(a ,@lst b) with lst=nil yields (a b)
        """

        expr, mod = self.compile_expr("`(a ,@lst b)", lst=nil)
        result = expr().run()
        self.assertEqual(
            [symbol("a"), symbol("b")],
            list(result.unpack()))


    def test_splice_result_is_proper(self):
        """
        Splice always produces a proper list
        """

        lst = self._make_list(1, 2)
        expr, mod = self.compile_expr("`(,@lst)", lst=lst)
        result = expr().run()
        self.assertTrue(result.is_proper())


class TestQuasiquoteDottedPair(SpecialsTestCase):
    """
    Quasi-quoting improper (dotted) lists.

    Note: `(a . ,x) at the reader level becomes the three-element proper list
    (compile/quasiquote (a compile/unquote x)) because the dotted-pair syntax
    `(X . (Y Z))` is identical to the list `(X Y Z)`.  Only atomic (non-pair)
    dotted tails produce genuine improper pairs.
    """

    def test_dotted_constant_tail(self):
        """
        `(a . b) yields (a . b) as an improper pair
        """

        expr, mod = self.compile_expr("`(a . b)")
        result = expr().run()
        self.assertEqual(symbol("a"), result[0])
        self.assertEqual(symbol("b"), result[1])
        self.assertFalse(result.is_proper())


    def test_dotted_number_tail(self):
        """
        `(a . 42) yields (a . 42) as an improper pair (atom tail)
        """

        expr, mod = self.compile_expr("`(a . 42)")
        result = expr().run()
        self.assertEqual(symbol("a"), result[0])
        self.assertEqual(42, result[1])
        self.assertFalse(result.is_proper())


    def test_dotted_multi_element_constant_tail(self):
        """
        `(a b . c) yields the improper list (a b . c)
        """

        expr, mod = self.compile_expr("`(a b . c)")
        result = expr().run()
        self.assertEqual(symbol("a"), result[0])
        self.assertEqual(symbol("b"), result[1][0])
        self.assertEqual(symbol("c"), result[1][1])
        self.assertFalse(result.is_proper())


class TestQuasiquoteNested(SpecialsTestCase):
    """
    Nested quasi-quote: inner quasiquotes increment the nesting level so
    inner unquotes are not evaluated until the outer quasiquote is active
    """

    def test_nested_quasiquote_inner_unquote_not_evaluated(self):
        """
        ``(a ,(+ 1 2)) at the outer level yields (compile/quasiquote (a (compile/unquote (+ 1 2))))
        as a literal data structure — the inner unquote is NOT evaluated
        """

        expr, mod = self.compile_expr("``(a ,(+ 1 2))")
        result = expr().run()
        # result is (compile/quasiquote (a (compile/unquote (+ 1 2))))
        self.assertEqual(symbol("compile/quasiquote"), result[0])
        inner = result[1][0]
        self.assertEqual(symbol("a"), inner[0])
        unquote_form = inner[1][0]
        self.assertEqual(symbol("compile/unquote"), unquote_form[0])


    def test_nested_quasiquote_level_tracking(self):
        """
        ``x yields (compile/quasiquote x) as a literal pair
        """

        expr, mod = self.compile_expr("``x")
        result = expr().run()
        self.assertEqual(symbol("compile/quasiquote"), result[0])
        self.assertEqual(symbol("x"), result[1][0])


class TestQuasiquoteErrors(SpecialsTestCase):
    """
    Error cases: unquote and unquote-splicing outside quasiquote
    """

    def test_unquote_outside_quasiquote_raises(self):
        """
        (compile/unquote x) at top level raises SpecialError
        """

        self.assertRaises(SpecialError, self.compile_expr, "(compile/unquote x)")


    def test_unquote_splicing_outside_quasiquote_raises(self):
        """
        (compile/unquote-splicing x) at top level raises SpecialError
        """

        self.assertRaises(
            SpecialError,
            self.compile_expr,
            "(compile/unquote-splicing x)")


    def test_unquote_splicing_outside_quasiquote_also_raises(self):
        """
        (compile/unquote-splicing x) at top level raises SpecialError
        """

        self.assertRaises(
            SpecialError,
            self.compile_expr,
            "(compile/unquote-splicing x)")


# The end.
