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
tests.reproducers.test_deep_closure_chain_release

Dropping a long chain of closures -- each function's namespace holding
a cell that holds the previous function -- releases it with bounded C
stack. The function, namespace and ref deallocs defer deep releases
through CPython's trashcan, as the pair and values deallocs already
did; without it each link cost three C frames and a chain a few
hundred thousand long overflowed the stack.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define mk [g]:
    (lambda [] g)

define chain [count]:
    define f None
    define n 0
    while (< n count):
        set! f (mk f)
        set! n (+ n 1)
    f
"""


class TestDeepClosureChainRelease(unittest.IsolatedAsyncioTestCase):

    async def test_dropping_a_300k_closure_chain(self):
        module = new_module("chain")
        vars(module)["__file__"] = "<chain>"
        await load_module_source_async(module, SRC)
        f = await vars(module)["chain"](300000)
        del f
        gc.collect()


# The end.
