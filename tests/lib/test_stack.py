# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
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
Unit tests for Stack type

Block style and handler state now live on the dynamic chain (dynchain);
Stack is storage-only. Tests for the former BlockStyle enum and
status/disable/push_block(style, ...) have been removed.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

from koskari.lib import Stack

import pytest

pytestmark = pytest.mark.phase_lib


class TestStack(unittest.IsolatedAsyncioTestCase):
    """Tests for the Stack type"""

    async def test_type_name(self):
        """Stack type has correct name"""

        self.assertEqual(Stack.__name__, "Stack")

    async def test_create(self):
        """Stack can be created without arguments"""

        s = Stack()
        self.assertIsInstance(s, Stack)

    async def test_create_no_args(self):
        """Stack rejects arguments"""

        with self.assertRaises(TypeError):
            Stack(1)

        with self.assertRaises(TypeError):
            Stack(foo=1)

    async def test_duplicate(self):
        """duplicate() copies elements on top of stack"""

        s = Stack()
        s.duplicate(0)
        self.assertEqual(s.length(), 0)
        self.assertRaises(RuntimeError, s.duplicate, 1)

        s = Stack()
        s.push(1)
        s.push(2)
        s.push(3)
        self.assertEqual(s.length(), 3)

        s.duplicate(0)
        self.assertEqual(s.length(), 3)

        s.duplicate(1)
        self.assertEqual(s.length(), 4)
        self.assertEqual(s.pop_tail(4), (1, 2, 3, 3))

        s = Stack()
        s.push(0)
        s.push(1)
        s.push(2)
        s.push(3)
        s.duplicate(4)
        self.assertEqual(s.length(), 8)
        s.push(9)
        self.assertEqual(s.length(), 9)
        self.assertEqual(s.pop_tail(9), (0, 1, 2, 3, 0, 1, 2, 3, 9))

        s = Stack()
        s.push(1)
        s.push(2)
        s.push(3)
        self.assertEqual(s.length(), 3)
        self.assertRaises(RuntimeError, s.duplicate, 4)

    async def test_replace(self):
        """replace() updates element at index"""

        s = Stack()
        s.push(1)
        s.push(2)
        s.push(3)

        s.replace(0, 101)
        s.replace(2, 303)
        self.assertRaises(RuntimeError, s.replace, 3, 999)

        self.assertEqual(s.pop(), 303)
        self.assertEqual(s.pop(), 2)
        self.assertEqual(s.pop(), 101)
        self.assertRaises(RuntimeError, s.replace, 0, 999)

    async def test_pull(self):
        """pull() copies values from parent block to current block"""

        s = Stack()
        s.push(1)
        s.push(2)
        s.push(3)
        s.push_block()
        s.push(0)
        s.pull(3)

        self.assertEqual(s.total(), 7)
        self.assertEqual(s.length(), 4)
        self.assertEqual(s.pop_tail(4), (0, 1, 2, 3))
        self.assertEqual(s.length(), 0)

        s.pop_block(0)
        self.assertEqual(s.total(), 3)
        self.assertEqual(s.length(), 3)
        self.assertEqual(s.pop_tail(3), (1, 2, 3))
        self.assertEqual(s.length(), 0)

        s = Stack()
        s.push_block()
        self.assertRaises(RuntimeError, s.pull, 1)

    async def test_pushpop(self):
        """push() and pop() work correctly"""

        s = Stack()
        self.assertRaises(RuntimeError, s.pop)
        self.assertRaises(RuntimeError, s.peek)

        s.push(1)
        self.assertEqual(s.peek(), 1)
        self.assertEqual(s.pop(), 1)
        self.assertRaises(RuntimeError, s.pop)
        self.assertRaises(RuntimeError, s.peek)

    async def test_poptail(self):
        """pop_tail() removes and returns multiple elements"""

        s = Stack()
        self.assertEqual(s.pop_tail(0), ())
        self.assertRaises(RuntimeError, s.pop_tail, 1)

        for i in range(0, 10):
            s.push(i)

        self.assertRaises(RuntimeError, s.pop_tail, 11)
        self.assertEqual(s.pop_tail(10), tuple(range(0, 10)))

        for i in range(0, 15):
            s.push(i)

        self.assertEqual(s.pop_tail(10), tuple(range(5, 15)))
        self.assertEqual(s.pop_tail(5), tuple(range(0, 5)))
        self.assertEqual(s.pop_tail(0), ())
        self.assertRaises(RuntimeError, s.pop)
        self.assertRaises(RuntimeError, s.pop_tail, 1)

    async def test_poptaildict(self):
        """pop_tail_dict() pops key/value pairs into a dict"""

        s = Stack()

        expect = {"a": 123, "b": 456, "c": 789}
        for k, v in expect.items():
            s.push(k)
            s.push(v)

        self.assertEqual(s.total(), 6)
        new = s.pop_tail_dict(3)

        self.assertEqual(new, expect)
        self.assertEqual(s.total(), 0)

    async def test_pushblock_popblock(self):
        """push_block() and pop_block() manage block hierarchy"""

        s = Stack()
        self.assertEqual(s.total(), 0)
        self.assertEqual(s.length(), 0)

        s.push(123)
        self.assertEqual(s.peek(), 123)
        s.push_block()
        self.assertRaises(RuntimeError, s.pop)
        s.push(456)
        self.assertEqual(s.peek(), 456)
        s.push(789)
        self.assertEqual(s.peek(), 789)
        s.pop_block(1)
        self.assertEqual(s.peek(), 789)
        self.assertEqual(s.pop(), 789)
        self.assertEqual(s.peek(), 123)
        self.assertEqual(s.pop(), 123)
        self.assertRaises(RuntimeError, s.peek)
        self.assertRaises(RuntimeError, s.pop)
        self.assertRaises(RuntimeError, s.pop_block, 0)

    async def test_push_block_no_args(self):
        """push_block() takes no arguments (style/labels moved to dynchain)"""

        s = Stack()
        s.push_block()
        self.assertEqual(s.depth(), 2)
        s.pop_block(0)
        self.assertEqual(s.depth(), 1)

    async def test_push_overrun(self):
        """Stack block has a maximum size limit"""

        s = Stack()
        for i in range(0, 256):
            s.push(i)
        self.assertEqual(s.total(), 256)
        self.assertRaises(RuntimeError, s.push, 999)
        self.assertEqual(s.pop(), 255)
        self.assertEqual(s.total(), 255)
        s.push(888)
        self.assertEqual(s.peek(), 888)
        self.assertRaises(RuntimeError, s.push, 999)

    async def test_popblock_overrun(self):
        """pop_block() respects parent block size limits"""

        s = Stack()
        for i in range(0, 200):
            s.push(i)
        self.assertEqual(s.peek(), 199)
        self.assertEqual(s.total(), 200)
        self.assertEqual(s.length(), 200)

        s.push_block()
        self.assertEqual(s.total(), 200)
        self.assertEqual(s.length(), 0)

        for i in range(200, 400):
            s.push(i)
        self.assertEqual(s.peek(), 399)
        self.assertEqual(s.total(), 400)
        self.assertEqual(s.length(), 200)

        self.assertRaises(RuntimeError, s.pop_block, 200)
        self.assertRaises(RuntimeError, s.pop_block, 57)
        s.pop_block(56)
        self.assertEqual(s.total(), 256)
        self.assertEqual(s.length(), 256)
        self.assertEqual(s.peek(), 399)
        self.assertRaises(RuntimeError, s.push, 123)

        s.push_block()
        self.assertEqual(s.total(), 256)
        self.assertEqual(s.length(), 0)
        s.pop_block(0)
        self.assertEqual(s.total(), 256)
        self.assertEqual(s.length(), 256)

    async def test_peek_at(self):
        """peek_at() accesses elements by index"""

        s = Stack()
        for i in range(0, 10):
            s.push(i)

        for i in range(0, 10):
            self.assertEqual(s.peek_at(i), i)
        self.assertRaises(RuntimeError, s.peek_at, 10)

        for i in range(1, 10):
            self.assertEqual(s.peek_at(-i), 10 - i)
        self.assertRaises(RuntimeError, s.peek_at, -11)

    async def test_extend(self):
        """extend() adds multiple elements from a sequence"""

        s = Stack()
        s.extend([1, 2, 3])
        self.assertEqual(s.length(), 3)
        self.assertEqual(s.pop_tail(3), (1, 2, 3))

    async def test_drop(self):
        """drop() removes elements without returning them"""

        s = Stack()
        s.push(1)
        s.push(2)
        s.push(3)
        s.drop(2)
        self.assertEqual(s.length(), 1)
        self.assertEqual(s.pop(), 1)

    async def test_depth(self):
        """depth() returns the number of blocks"""

        s = Stack()
        self.assertEqual(s.depth(), 1)

        s.push_block()
        self.assertEqual(s.depth(), 2)

        s.push_block()
        self.assertEqual(s.depth(), 3)

        s.pop_block(0)
        self.assertEqual(s.depth(), 2)

    async def test_total(self):
        """total() returns sum of all block lengths"""

        s = Stack()
        s.push(1)
        s.push(2)
        s.push_block()
        s.push(3)

        self.assertEqual(s.total(), 3)
        self.assertEqual(s.length(), 1)

    async def test_view(self):
        """view() returns tuple of current block's stack"""

        s = Stack()
        s.push(1)
        s.push(2)
        s.push(3)

        self.assertEqual(s.view(), (1, 2, 3))

    async def test_clear(self):
        """clear() empties all blocks except root"""

        s = Stack()
        s.push(1)
        s.push_block()
        s.push(2)
        s.push_block()
        s.push(3)

        self.assertEqual(s.depth(), 3)
        self.assertEqual(s.total(), 3)

        s.clear()
        self.assertEqual(s.depth(), 1)
        self.assertEqual(s.total(), 0)
        self.assertEqual(s.length(), 0)

    async def test_drop_block(self):
        """drop_block() removes current block without transferring"""

        s = Stack()
        s.push(1)
        s.push_block()
        s.push(2)
        s.push(3)

        self.assertEqual(s.total(), 3)
        self.assertEqual(s.length(), 2)

        s.drop_block()
        self.assertEqual(s.total(), 1)
        self.assertEqual(s.length(), 1)
        self.assertEqual(s.pop(), 1)

# The end.
