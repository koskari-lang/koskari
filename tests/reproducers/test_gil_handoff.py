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
GIL handoff from a running Koskari loop

A Koskari loop that calls nothing Python holds the GIL until a
cooperation point finds the interpreter's drop request up and the
trampoline yields. A second thread that only needs the GIL to make
progress therefore proves the handoff: it records timestamps in a
tight Python loop while the main thread runs a pure Koskari loop for
a fixed wall-clock span, and at least one of its stamps must fall
strictly inside that span. Without the handoff the thread runs only
before the loop starts or after it ends.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import threading
import time

import pytest

from tests.compile.specials import SpecialsTestCase
from tests import needs_threads


pytestmark = pytest.mark.phase_reproducers


@needs_threads
class TestGilHandoff(SpecialsTestCase):

    async def test_koskari_loop_yields_to_a_python_thread(self):
        """
        A Python thread makes progress while a pure Koskari loop runs
        """

        src = """
        (let [[deadline (+ (time.monotonic) 0.4)] [n 0]]
          (while (< (time.monotonic) deadline)
            (setq n (+ n 1)))
          n)
        """

        stamps = []
        stop = threading.Event()

        def ticker():
            while not stop.is_set():
                stamps.append(time.monotonic())

        expr, _mod = self.compile_expr(src, time=time)
        thread = threading.Thread(target=ticker)
        thread.start()
        try:
            started = time.monotonic()
            iterations = await expr()
            ended = time.monotonic()
        finally:
            stop.set()
            thread.join(5)

        self.assertGreater(iterations, 0)
        inside = [t for t in stamps if started + 0.05 < t < ended - 0.05]
        self.assertTrue(
            inside,
            f"the Python thread never ran during the loop; {len(stamps)}"
            " stamps, none inside the span")


# The end.
