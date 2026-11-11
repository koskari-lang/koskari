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
tests.reproducers.test_asyncgen_exceptional_unwind

Reproducer: an exception unwinding a frame that holds a suspended async
generator must propagate cleanly. The generator wrapper's refcount hits
zero mid-unwind, and its dealloc currently re-enters the evaluator to
drive a GeneratorExit teardown with the in-flight exception still set
(asyncgen_teardown_chain_on_ctx, iter.c) -- corrupting teardown. Under
the Python-await drive path this SIGSEGVs the process; under the
module-load drive path the in-flight exception is destroyed
(PyErr_Clear on the teardown's RAISE result) and the driver's
continuation is silently discarded, surfacing only later as an empty
binding.

The control case (same exception, no suspended generator in the frame)
propagates correctly, isolating the suspended generator as the
necessary ingredient. Correct behavior asserted here: the AttributeError
reaches the caller under both drive paths, and the frame's remaining
forms never run for the wrong reason (exception, not silent loss).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


PROBE_SRC = """
(def g [] (yield 1) (yield 2))

(def control []
  (var lst #[1 2])
  (lst.nope)
  "unreached")

(def probe []
  (var gen (g))
  (gen.__anext__)
  (gen.nope)
  "unreached")
"""


# Module-load drive: the raise happens inside a top-level define's
# expression. The load itself must propagate the AttributeError.
MODULE_DRIVE_SRC = PROBE_SRC + """
(var result (probe))
"""


class TestAsyncGenExceptionalUnwind(unittest.IsolatedAsyncioTestCase):
    """
    An exception crossing a frame that holds a suspended async generator
    propagates cleanly
    """

    async def load(self, name, source):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, source)
        return module

    async def test_control_no_generator(self):
        """
        The same exception with no suspended generator in the frame
        propagates cleanly (isolates the trigger)
        """

        mod = await self.load("agen_unwind_control", PROBE_SRC)
        with self.assertRaises(AttributeError):
            await vars(mod)["control"]()

    async def test_python_await_drive_propagates(self):
        """
        Python-await drive: the AttributeError reaches the awaiting
        caller (currently SIGSEGVs the process)
        """

        mod = await self.load("agen_unwind_await", PROBE_SRC)
        with self.assertRaises(AttributeError):
            await vars(mod)["probe"]()

    async def test_module_drive_propagates(self):
        """
        Module-load drive: the AttributeError propagates out of the load
        (currently swallowed; the define ends up empty and the load
        reports success)
        """

        with self.assertRaises(AttributeError):
            await self.load("agen_unwind_module", MODULE_DRIVE_SRC)


# The end.
