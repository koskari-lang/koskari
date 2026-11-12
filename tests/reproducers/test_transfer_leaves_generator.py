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
tests.reproducers.test_transfer_leaves_generator

A full continuation applied inside a generator that a for-each is
stepping leaves that generator, and every generator it was stepping in
turn: each is finished on the way out, so a later next on it raises
StopIteration.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def inner [k]:
    yield 1
    (k "escaped")
    yield 2

def outer [k box]:
    (set! box[1] (inner k))
    for-each [x box[1]]:
        yield x

def finished [g]:
    try:
        (next g)
        "resumed"
    except [e StopIteration]:
        "finished"

def escape_two_deep []:
    var box #[None None]
    var seen #[]
    var r (let/cc k (begin
                      (set! box[0] (outer k box))
                      (for-each [x box[0]] (seen.append x))
                      "done"))
    #[r seen (finished box[0]) (finished box[1])]
"""


class TestTransferLeavesGenerator(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("transfer_leaves_generator")
        vars(module)["__file__"] = "<transfer_leaves_generator>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_escape_finishes_both_generators(self):
        mod = await self.load()
        self.assertEqual(
            ["escaped", [1], "finished", "finished"],
            await mod["escape_two_deep"]())


# The end.
