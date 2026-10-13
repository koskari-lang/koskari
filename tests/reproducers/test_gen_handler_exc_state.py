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
tests.reproducers.test_gen_handler_exc_state

A generator suspended inside an except handler keeps its handled
exception to itself, as a Python generator does: the consumer sees its
own handled exception, or none, while the generator is suspended, and
the generator sees its own again on resume. When the generator leaves
the handler it gives back the consumer's handled exception as it is
then, not as it was when the handler began. A generator with no
handled exception of its own sees the consumer's.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import sys

def exc_now []:
    (sys.exc_info)[1]

def gen []:
    try:
        raise (ValueError "GA")
    except [e ValueError]:
        yield 1
        yield (exc_now)
    yield (exc_now)

def nested_gen []:
    try:
        raise (ValueError "outer")
    except [e ValueError]:
        try:
            raise (TypeError "inner")
        except [f TypeError]:
            yield 1
            yield (exc_now)
        yield (exc_now)
    yield (exc_now)

def plain_gen []:
    yield (exc_now)
    yield (exc_now)

def consumer []:
    var g (gen)
    var seen #[]
    try:
        raise (KeyError "X")
    except [e KeyError]:
        (next g)
        (seen.append (exc_now))
    (seen.append (exc_now))
    (seen.append (next g))
    (seen.append (next g))
    (seen.append (exc_now))
    try:
        raise (IndexError "new")
    except [e IndexError]:
        (seen.append e.__context__)
    seen

def nested_consumer []:
    var g (nested_gen)
    var seen #[]
    try:
        raise (KeyError "X")
    except [e KeyError]:
        (next g)
        (seen.append (exc_now))
    (seen.append (exc_now))
    (seen.append (next g))
    (seen.append (next g))
    (seen.append (next g))
    (seen.append (exc_now))
    seen

def plain_consumer []:
    var g (plain_gen)
    var seen #[]
    try:
        raise (KeyError "X")
    except [e KeyError]:
        (seen.append (next g))
    (seen.append (next g))
    seen
"""


def names(seen):
    return [None if v is None else type(v).__name__ for v in seen]


class TestGenHandlerExcState(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("gen_handler_exc_state")
        vars(module)["__file__"] = "<gen_handler_exc_state>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_koskari_consumer(self):
        mod = await self.load()
        seen = await mod["consumer"]()
        self.assertEqual(
            ["KeyError", None, "ValueError", None, None, None],
            names(seen))


    async def test_nested_regions(self):
        mod = await self.load()
        seen = await mod["nested_consumer"]()
        self.assertEqual(
            ["KeyError", None, "TypeError", "ValueError", None, None],
            names(seen))


    async def test_generator_without_handler_sees_consumer(self):
        mod = await self.load()
        seen = await mod["plain_consumer"]()
        self.assertEqual(["KeyError", None], names(seen))


    async def test_python_driver(self):
        mod = await self.load()
        g = iter(mod["gen"]())
        seen = []
        try:
            raise KeyError("X")
        except KeyError:
            next(g)
            seen.append(sys.exc_info()[1])
        seen.append(sys.exc_info()[1])
        seen.append(next(g))
        seen.append(next(g))
        seen.append(sys.exc_info()[1])
        self.assertEqual(
            ["KeyError", None, "ValueError", None, None], names(seen))


# The end.
