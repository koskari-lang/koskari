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
Handler-visible exception state (exc_info) across await suspensions.

The evaluator stages the handled exception into Python's thread-state
exc_info for handler bytecode; that state must be per-context, surviving
suspension and interleaving with other contexts on the same thread.
"""

import asyncio

import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def _probe():
    import sys
    exc = sys.exc_info()[1]
    return type(exc).__name__ if exc is not None else "none"


class TestExcInfoIsolation(SpecialsTestCase):

    async def test_exc_info_visible_after_await_in_handler(self):
        """
        A single suspended handler still sees its exception on resume.
        """

        async def snooze():
            await asyncio.sleep(0)
            return None

        def boom(v):
            raise ValueError("boom")

        src_str = """
        (try
          (boom 1)
          ([ValueError]
            (begin
              (snooze)
              (probe))))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, snooze=snooze, probe=_probe,
            ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "ValueError")


    async def test_exc_info_across_interleaved_tasks(self):
        """
        Two contexts suspended inside their handlers on one thread each see
        their own exception on resume.
        """

        async def snooze():
            for _ in range(3):
                await asyncio.sleep(0)
            return None

        def make(exc_type):
            def boom(v):
                raise exc_type("boom")

            src_str = """
            (try
              (boom 1)
              ([Exc]
                (begin
                  (snooze)
                  (probe))))
            """
            expr, mod = self.compile_expr(
                src_str, boom=boom, snooze=snooze, probe=_probe,
                Exc=exc_type)
            return expr

        a, b = await asyncio.gather(
            make(ValueError)(), make(KeyError)())
        self.assertEqual((a, b), ("ValueError", "KeyError"))


# The end.
