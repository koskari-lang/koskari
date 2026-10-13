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
the driver's view is never touched, even while the generator is
suspended mid-extent, and an abandoned generator takes its table with
it.

The extent-safe suspension methodology remains delimited continuations
(shift/reset), whose transfer plans unwind and rewind PARAM extents per
application: yield does not unwind; shift does.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


# The generator reports its own view of `g` at each step; the driver
# reads its own view while the generator is suspended mid-extent.
REPORTER_SRC = """
(var reporter (lambda []
  (parameterize [[g "gen-binding"]]
    (yield (parameterq g "unset-1"))
    (yield (parameterq g "unset-2")))
  (yield (parameterq g "after-extent"))))

(var probe (lambda []
  (var out #{})
  (var gen (reporter))
  (set! (item out "first") (gen.__anext__))
  (set! (item out "driver-between") (parameterq g "outer"))
  (set! (item out "second") (gen.__anext__))
  (set! (item out "third") (gen.__anext__))
  out))

(var results (probe))
"""


# Exhaustion and abandonment both leave the driver's view untouched: the
# generator's extent lived and died in its own table.
LIFECYCLE_SRC = """
(import-from gc collect)

(var counter (lambda []
  (parameterize [[g "gen-binding"]]
    (yield 1)
    (yield 2))))

(var probe (lambda []
  (var out #{})

  (var gen1 (counter))
  (gen1.__anext__)
  (set! (item out "between") (parameterq g "outer"))
  (for-each [x gen1] None)
  (set! (item out "exhausted") (parameterq g "outer"))

  (var gen2 (counter))
  (gen2.__anext__)
  (set! gen2 None)
  (collect)
  (set! (item out "abandoned") (parameterq g "outer"))

  out))

(var results (probe))
"""


# A Koskari consumer steps a Koskari generator on its own chain, so the
# generator's parameterize extent swaps cells in the consumer's table.
# The extent is frozen with its span and returns with the install:
# the consumer never reads the generator's binding, the generator's own
# reads see it across suspensions, and the block exit restores what the
# consumer had at the latest resume, not at the first.
FLATTENED_SRC = """
(var reporter (lambda []
  (parameterize [[g "gen"]]
    (yield (parameterq g "unset"))
    (yield (parameterq g "unset")))
  (yield (parameterq g "unset"))))

(var stepped (lambda []
  (var out #[])
  (var it (reporter))
  (parameterize [[g "c"]]
    (out.append (next it))
    (out.append (+ "in-c:" (parameterq g "unset"))))
  (out.append (+ "after-c:" (parameterq g "unset")))
  (out.append (next it))
  (out.append (+ "after-2:" (parameterq g "unset")))
  (out.append (next it))
  (out.append (+ "after-3:" (parameterq g "unset")))
  out))

(var looped (lambda []
  (var out #[])
  (for-each [x (reporter)]
    (out.append (+ x ":" (parameterq g "unset"))))
  (out.append (+ "done:" (parameterq g "unset")))
  out))

(var stepped-results (stepped))
(var looped-results (looped))
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
        the driver's view is never touched while the generator is
        suspended mid-extent
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


    async def test_flattened_extent_freezes_with_the_span(self):
        """
        A generator stepped on the consumer's chain keeps its binding for
        its own reads, never shows it to the consumer while suspended,
        and restores the consumer's current cell when its block exits
        """

        mod = await self.load("param_gen_flattened", FLATTENED_SRC)
        self.assertEqual(vars(mod)["stepped-results"], [
            "gen", "in-c:c", "after-c:unset",
            "gen", "after-2:unset",
            "unset", "after-3:unset",
        ])
        self.assertEqual(vars(mod)["looped-results"], [
            "gen:unset", "gen:unset", "unset:unset", "done:unset",
        ])


# The end.
