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
tests.reproducers.test_const_fold_drops_call

Full-stack reproducer: a begin form whose last element is a constant,
placed where the compiler considers constant folding (an operator
operand, a list literal element, an item key). The preceding forms in
the begin must still run.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(define calls #[])
(define f (lambda [] (calls.append 1) None))
(define d #{"k": "v"})

(define op-result (+ (begin (f) 1) 2))
(define list-result #[(begin (f) 1)])
(define item-result (item d (begin (f) "k")))
"""


class TestConstFoldDropsCall(unittest.IsolatedAsyncioTestCase):
    """
    Forms preceding a constant inside a folded operand still execute
    """

    async def asyncSetUp(self):
        module = new_module("const_fold_drops_call")
        vars(module)["__file__"] = "<const_fold_drops_call>"
        await load_module_source_async(module, SRC, layout=False)
        self.ns = vars(module)

    async def test_operator_operand(self):
        self.assertEqual(3, self.ns["op-result"])

    async def test_list_element(self):
        self.assertEqual([1], self.ns["list-result"])

    async def test_item_key(self):
        self.assertEqual("v", self.ns["item-result"])

    async def test_every_call_ran(self):
        self.assertEqual([1, 1, 1], self.ns["calls"])


# The end.
