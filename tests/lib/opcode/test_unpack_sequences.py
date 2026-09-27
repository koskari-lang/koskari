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
(define pair_sum (lambda [p]
  (define-values [a b] p)
  (+ a b)))

(define pair_items (lambda [p]
  (define-values [a b] p)
  (#list a b)))
"""


class TestUnpackSequences(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("unpack_sequences")
        vars(module)["__file__"] = "<unpack_sequences>"
        await load_module_source_async(module, SRC, layout=False)
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
        self.assertIn("expected 2 elements, got 3", str(caught.exception))
        with self.assertRaises(ValueError) as caught:
            await mod["pair_sum"]([1])
        self.assertIn("expected 2 elements, got 1", str(caught.exception))

    async def test_non_iterable_reports_the_type(self):
        mod = await self.load()
        with self.assertRaises(TypeError) as caught:
            await mod["pair_sum"](7)
        self.assertIn("cannot unpack non-iterable int object",
                      str(caught.exception))

    async def test_shared_tuple_is_untouched(self):
        mod = await self.load()
        shared = (10, 20)
        for _ in range(50):
            self.assertEqual(await mod["pair_sum"](shared), 30)
        self.assertEqual(shared, (10, 20))


# The end.
