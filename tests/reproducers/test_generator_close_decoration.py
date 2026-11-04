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
tests.reproducers.test_generator_close_decoration

Closing a Koskari generator drives GeneratorExit through its body. The
finally clauses run, a close that completes is quiet, an exception a
finally raises in its place propagates with the Koskari traceback
decoration, and an abandoned generator tears down without anything
surfacing.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(var log (list))

(def plain []
  (yield 1)
  (yield 2))

(def with_cleanup []
  (try (begin (yield 1) (yield 2))
    [finally: (log.append "cleanup")]))

(def cleanup_raises []
  (try (begin (yield 1) (yield 2))
    [finally: (raise (ValueError "cleanup failed"))]))

(def raises_generator_exit []
  (raise (GeneratorExit "deliberate")))
"""


class TestGeneratorCloseDecoration(unittest.IsolatedAsyncioTestCase):
    """
    GeneratorExit driven by close runs finally clauses and stays quiet;
    an exception raised in its place keeps its decoration
    """

    async def load(self):
        module = new_module("generator_close_decoration")
        vars(module)["__file__"] = "<generator_close_decoration>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_aclose_runs_finally_quietly(self):
        mod = await self.load()
        agen = mod["with_cleanup"]()
        self.assertEqual(await agen.__anext__(), 1)
        self.assertIsNone(await agen.aclose())
        self.assertEqual(list(mod["log"]), ["cleanup"])

    async def test_aclose_without_handler_is_quiet(self):
        mod = await self.load()
        agen = mod["plain"]()
        self.assertEqual(await agen.__anext__(), 1)
        self.assertIsNone(await agen.aclose())
        with self.assertRaises(StopAsyncIteration):
            await agen.__anext__()

    async def test_exception_raised_by_finally_keeps_decoration(self):
        mod = await self.load()
        agen = mod["cleanup_raises"]()
        self.assertEqual(await agen.__anext__(), 1)
        with self.assertRaises(ValueError) as ctx:
            await agen.aclose()
        exc = ctx.exception
        self.assertIn("cleanup failed", str(exc))
        self.assertTrue(hasattr(exc, "__koskari_dynchain__"))

    async def test_deliberate_generator_exit_still_propagates(self):
        mod = await self.load()
        with self.assertRaises(GeneratorExit) as ctx:
            await mod["raises_generator_exit"]()
        self.assertIn("deliberate", str(ctx.exception))

    async def test_abandoned_generator_tears_down_quietly(self):
        mod = await self.load()
        unraisable = []
        agen = mod["with_cleanup"]()
        self.assertEqual(await agen.__anext__(), 1)
        prior = sys.unraisablehook
        sys.unraisablehook = unraisable.append
        try:
            del agen
            gc.collect()
            # at rest, the dropped generator closes in the aclose the
            # running loop's finalizer hook schedules
            for _ in range(5):
                await asyncio.sleep(0)
        finally:
            sys.unraisablehook = prior
        self.assertEqual(unraisable, [])
        self.assertEqual(list(mod["log"]), ["cleanup"])


# The end.
