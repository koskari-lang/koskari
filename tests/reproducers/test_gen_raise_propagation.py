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
tests.reproducers.test_gen_raise_propagation

Reproducer: an exception raised inside a flattened generator killed the
consumer's context instead of propagating -- the raise walk consumed the
event at the GEN boundary and marked the whole context dead, bypassing
every TRY/FINALLY region in the consumer. A leaked StopIteration then
read as coroutine-protocol completion at the drive boundary, silently
truncating the enclosing form; any other exception escaped to the top
with a mislocated traceback.

Fixed semantics (Python's): an exception in a generator propagates into
the consumer at the iteration point, catchable there; the generator is
finished by the exception. One protocol judgment sits with the iterating
boundary itself: a StopIteration arriving from its in-flight flattened
step is consumed as end-of-iteration (the same classification the
Python-iterator step path makes), so the pre-479 leak idiom ends the
loop normally instead of propagating. Boundary-driven generators are
unchanged -- their raise exits the context to the Python driver, which
translates it.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define boomgen []:
    (yield 1)
    (yield 2)
    (raise (ValueError "boom"))

define leakgen []:
    define r (iter (range 0 3))
    while True:
        (yield (next r))

define catch-boom []:
    define col #[]
    try:
        (for-each [v (boomgen)]
            (col.append v))
        "no-raise"
    except [ValueError]:
        #["caught" col]

define catch-leak []:
    define col #[]
    try:
        (for-each [v (leakgen)]
            (col.append v))
        #["completed" col]
    except [StopIteration]:
        #["caught" col]

define uncaught-boom []:
    (for-each [v (boomgen)] None)
    "unreached"

define closed-after []:
    define gen (boomgen)
    try:
        (for-each [v gen] None)
    except [ValueError]:
        None
    ;; the exception finished the generator: nothing more to yield
    define col #[]
    (for-each [v gen]
        (col.append v))
    col
"""


class TestGenRaisePropagation(unittest.IsolatedAsyncioTestCase):
    """
    Exceptions raised inside flattened generators propagate to consumer
    handlers; the generator is finished by the exception
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_consumer_catches_flattened_raise(self):
        """
        a ValueError from the generator body is caught by the consumer's
        try, with the values yielded before the raise intact
        """

        mod = await self.load("grp_catch")
        result = await vars(mod)["catch-boom"]()
        self.assertEqual(result, ["caught", [1, 2]])

    async def test_leaked_stopiteration_ends_iteration(self):
        """
        a StopIteration leaked from the body (pre-479 idiom) is consumed
        by the iterating boundary as end-of-iteration: the loop completes
        normally with the values yielded before the leak
        """

        mod = await self.load("grp_leak")
        result = await vars(mod)["catch-leak"]()
        self.assertEqual(result, ["completed", [0, 1, 2]])

    async def test_uncaught_raise_reaches_python(self):
        """
        with no consumer handler, the exception propagates out of the
        koskari callable to the awaiting Python caller
        """

        mod = await self.load("grp_uncaught")
        with self.assertRaises(ValueError):
            await vars(mod)["uncaught-boom"]()

    async def test_generator_finished_by_raise(self):
        """
        the exception finishes the generator: a later iteration yields
        nothing
        """

        mod = await self.load("grp_closed")
        result = await vars(mod)["closed-after"]()
        self.assertEqual(result, [])

    async def test_boundary_drive_unchanged(self):
        """
        boundary-driven generators keep their semantics: the raise exits
        the context and surfaces through the async iteration protocol
        """

        mod = await self.load("grp_boundary")
        agen = vars(mod)["boomgen"]()
        self.assertEqual(await agen.__anext__(), 1)
        self.assertEqual(await agen.__anext__(), 2)
        with self.assertRaises(ValueError):
            await agen.__anext__()


# The end.
