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
tests.reproducers.test_param_generator_extents

Generator extent semantics under per-context parameter cells. Generators
follow the Python model within their own world: yield does not unwind,
so a parameterize extent persists across the generator's suspensions and
undoes when its block exits or the generator is exhausted. Because an
async generator owns its context (cells forked at creation, like a
defer), that whole lifecycle plays out in the generator's private table:
the driver's view is never touched, even while the generator is parked
mid-extent, and an abandoned generator takes its table with it.

The extent-safe suspension methodology remains delimited continuations
(shift/reset), whose transfer plans unwind and rewind PARAM extents per
application: yield does not unwind; shift does.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


# The generator reports its own view of `g` at each step; the driver
# reads its own view while the generator is parked mid-extent.
REPORTER_SRC = """
(define reporter (lambda []
  (parameterize [[g "gen-binding"]]
    (yield (parameterq g "unset-1"))
    (yield (parameterq g "unset-2")))
  (yield (parameterq g "after-extent"))))

(define probe (lambda []
  (define out #{})
  (define gen (reporter))
  (set! (item out "first") (gen.__anext__))
  (set! (item out "driver-between") (parameterq g "outer"))
  (set! (item out "second") (gen.__anext__))
  (set! (item out "third") (gen.__anext__))
  out))

(define results (probe))
"""


# Exhaustion and abandonment both leave the driver's view untouched: the
# generator's extent lived and died in its own table.
LIFECYCLE_SRC = """
(import-from gc collect)

(define counter (lambda []
  (parameterize [[g "gen-binding"]]
    (yield 1)
    (yield 2))))

(define probe (lambda []
  (define out #{})

  (define gen1 (counter))
  (gen1.__anext__)
  (set! (item out "between") (parameterq g "outer"))
  (for-each [x gen1] None)
  (set! (item out "exhausted") (parameterq g "outer"))

  (define gen2 (counter))
  (gen2.__anext__)
  (set! gen2 None)
  (collect)
  (set! (item out "abandoned") (parameterq g "outer"))

  out))

(define results (probe))
"""


class TestParamGeneratorExtents(unittest.IsolatedAsyncioTestCase):
    """
    A generator's parameterize extents live in its own context: Python
    yield semantics inside, driver isolation outside
    """

    async def load(self, name, source):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, source)
        return module

    async def test_generator_extent_isolated_from_driver(self):
        """
        yield does not unwind the generator's extent (its own reads see
        the binding across suspensions; the block exit undoes it), and
        the driver's view is never touched while the generator is parked
        mid-extent
        """

        mod = await self.load("param_gen_reporter", REPORTER_SRC)
        out = vars(mod)["results"]
        self.assertEqual(out["first"], "gen-binding")
        self.assertEqual(out["driver-between"], "outer")
        self.assertEqual(out["second"], "gen-binding")
        self.assertEqual(out["third"], "after-extent")

    async def test_exhaustion_and_abandonment_leave_driver_view(self):
        """
        A generator exhausted or abandoned mid-extent leaves the driver's
        parameter view untouched: the extent lived in the generator's own
        table
        """

        mod = await self.load("param_gen_lifecycle", LIFECYCLE_SRC)
        out = vars(mod)["results"]
        self.assertEqual(out["between"], "outer")
        self.assertEqual(out["exhausted"], "outer")
        self.assertEqual(out["abandoned"], "outer")


# The end.
