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
tests.lib.opcode.test_unpack_sequences

Unpacking into two names takes a tuple, a list, a generator, and a
string alike, rejects a wrong length and a non-iterable with the same
messages, and leaves a shared tuple untouched.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define [pair_sum p]
  (define-values [a b] p)
  (+ a b))

(define [pair_items p]
  (define-values [a b] p)
  (#list a b))

(define [nested_pair p]
  (define-values [a [b c]] p)
  (#list a b c))

(define [star_pair p]
  (define-values [a *: b c] p)
  (#list a b c))
"""


MODULE_SRC = """
(define-values [a b] subject)
"""


class TestUnpackSequences(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("unpack_sequences")
        vars(module)["__file__"] = "<unpack_sequences>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def module_unpack(self, subject):
        module = new_module("unpack_sequences_module")
        vars(module)["__file__"] = "<unpack_sequences_module>"
        vars(module)["subject"] = subject
        await load_module_source_async(module, MODULE_SRC, layout=False)
        return vars(module)

    async def test_tuple_list_generator_and_string(self):
        mod = await self.load()
        self.assertEqual(await mod["pair_sum"]((2, 3)), 5)
        self.assertEqual(await mod["pair_sum"]([4, 5]), 9)
        self.assertEqual(await mod["pair_sum"](x for x in (6, 7)), 13)
        self.assertEqual(await mod["pair_items"]("ab"), ["a", "b"])

    async def test_wrong_length_reports_the_count(self):
        mod = await self.load()
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"]((1, 2, 3))
        self.assertIn("(expected 2, got 3)", str(caught.exception))
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"]([1])
        self.assertIn("(expected 2, got 1)", str(caught.exception))

    async def test_exact_wording(self):
        mod = await self.load()
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"]((1, 2, 3))
        self.assertEqual(str(caught.exception),
                         "too many values to unpack (expected 2, got 3)")
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"](x for x in (1, 2, 3))
        self.assertEqual(str(caught.exception),
                         "too many values to unpack (expected 2)")
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"]([1])
        self.assertEqual(str(caught.exception),
                         "not enough values to unpack (expected 2, got 1)")

    async def test_every_binding_kind_gives_the_same_text(self):
        mod = await self.load()
        short = "not enough values to unpack (expected 2, got 1)"
        long = "too many values to unpack (expected 2, got 3)"

        # a local pair, a module-level pair, and a nested pattern's
        # outer level
        for subject, want in (([1], short), ((1, 2, 3), long)):
            with self.assertRaises(ValueError) as caught:
                await mod["pair_sum"](subject)
            self.assertEqual(str(caught.exception), want)
            with self.assertRaises(ValueError) as caught:
                await self.module_unpack(subject)
            self.assertEqual(str(caught.exception), want)
            with self.assertRaises(ValueError) as caught:
                await mod["nested_pair"](subject)
            self.assertEqual(str(caught.exception), want)

        # a star binding
        with self.assertRaises(ValueError) as caught:
            await mod["star_pair"]((1,))
        self.assertEqual(
            str(caught.exception),
            "not enough values to unpack (expected at least 2, got 1)")

    async def test_non_iterable_reports_the_type(self):
        mod = await self.load()
        with self.assertRaises(TypeError) as caught:
            await mod["pair_sum"](7)
        self.assertIn("cannot unpack non-iterable int object",
                      str(caught.exception))

    async def test_non_iterable_has_no_prefix(self):
        mod = await self.load()
        for name in ("pair_sum", "nested_pair", "star_pair"):
            with self.assertRaises(TypeError) as caught:
                await mod[name](7)
            self.assertEqual(str(caught.exception),
                             "cannot unpack non-iterable int object")
        with self.assertRaises(TypeError) as caught:
            await self.module_unpack(7)
        self.assertEqual(str(caught.exception),
                         "cannot unpack non-iterable int object")

    async def test_shared_tuple_is_untouched(self):
        mod = await self.load()
        shared = (10, 20)
        for _ in range(50):
            self.assertEqual(await mod["pair_sum"](shared), 30)
        self.assertEqual(shared, (10, 20))

    async def test_over_long_iterator_is_not_drained(self):
        mod = await self.load()
        pulls = []

        def counting():
            for n in range(8):
                pulls.append(n)
                yield n
            raise AssertionError("drained past the arity check")

        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"](counting())
        self.assertIn("(expected 2)", str(caught.exception))
        self.assertEqual(3, len(pulls))

    async def test_short_iterator_reports_the_count(self):
        mod = await self.load()
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"](iter([1]))
        self.assertIn("(expected 2, got 1)", str(caught.exception))


# The end.
