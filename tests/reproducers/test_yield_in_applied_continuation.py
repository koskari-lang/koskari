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
tests.reproducers.test_yield_in_applied_continuation

A delimited continuation captured inside a generator can outlive it. A
reset's body is a function the reset calls, not part of the generator
around it, so a yield written in the body is refused when the source is
compiled, and the continuation never holds one. The generator yields
the reset's value instead, and a continuation captured there resumes
where no generator runs.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import SpecialError
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


YIELD_SRC = """
def gen [out]:
    (out.append (reset (+ (shift k k) (yield 5))))
"""


SRC = """
def gen [out]:
    yield (reset (+ (shift k (begin (out.append k) 1)) 10))

def escaped []:
    var out #[]
    var seen (list (gen out))
    #[seen ((out.pop) 5)]
"""


class TestYieldInAppliedContinuation(unittest.IsolatedAsyncioTestCase):

    async def load(self, src=SRC):
        module = new_module("yield_in_applied_continuation")
        vars(module)["__file__"] = "<yield_in_applied_continuation>"
        await load_module_source_async(module, src)
        return vars(module)


    async def test_yield_in_a_reset_body_is_refused(self):
        with self.assertRaises(SpecialError):
            await self.load(YIELD_SRC)


    async def test_continuation_from_a_generator_resumes_outside_it(self):
        mod = await self.load()
        self.assertEqual([[1], 15], await mod["escaped"]())


# The end.
