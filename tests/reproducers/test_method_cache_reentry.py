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
A method-cache hit probes the receiver's instance dict for a shadowing
attribute, and the probe runs a key's __eq__. An __eq__ that re-runs
the same call site with a receiver of another type refills the slot
under the hit; the hit must answer from what it read before the probe.
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
@demand
def callm [o]:
    (o.m)
"""


class TestMethodCacheReentry(unittest.IsolatedAsyncioTestCase):

    async def test_hit_answers_from_the_mode_read_before_the_probe(self):
        module = new_module("method_cache_reentry")
        vars(module)["__file__"] = "<method_cache_reentry>"
        await load_module_source_async(module, SRC)
        callm = vars(module)["callm"]

        class A:
            def m(self):
                return "A.m with self"

        class F:
            def __call__(self, *args):
                return f"F called with {len(args)} args"

        class B:
            m = F()

        class Key:
            armed = False

            def __hash__(self):
                return hash("m")

            def __eq__(self, other):
                if Key.armed:
                    Key.armed = False
                    callm(B())
                return False

        self.assertEqual("A.m with self", callm(A()))
        a = A()
        a.__dict__[Key()] = 1
        Key.armed = True
        self.assertEqual("A.m with self", callm(a))
        self.assertEqual("F called with 0 args", callm(B()))


# The end.
