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
Signal polling inside the evaluator

The evaluator tests the interpreter's eval breaker at its cooperation
points -- a loop's back-edge, a frame install, a resume hop, and every
re-entry through the prologue -- so a signal raised while Koskari code
runs interrupts any stretch that cycles through one of them. Each test
arms the interrupt from inside the running code and then runs such a
stretch: a tight counting loop, a deep non-tail recursion, and two
native filters looping resume to resume. The Koskari code catches the
interrupt itself, so a signal the evaluator failed to poll would
surface only after the stretch completed, on return to Python, and
fail the test. A straight line with no cooperation point in it runs to
its end first; the signal is then taken at the next point after it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import signal

from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


class Interrupted(Exception):
    pass


def _interrupt_handler(signum, frame):
    raise Interrupted()


class TestSignalPoll(SpecialsTestCase):

    def setUp(self):
        super().setUp()
        self._prior = signal.signal(signal.SIGINT, _interrupt_handler)


    def tearDown(self):
        signal.signal(signal.SIGINT, self._prior)
        super().tearDown()


    async def test_straight_line_then_loop(self):
        """
        A straight-line stretch with no cooperation point in it runs to
        its end; the signal is taken at the first back-edge after it
        """

        stores = " ".join("(setq x 0)" for _ in range(3000))
        src = f"""
        (let [[x 1] [n 0]]
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              {stores}
              (while (< n 1)
                (setq n (+ n 1)))
              x)
            ((e Interrupted) "caught")))
        """

        expr, _mod = self.compile_expr(src, Interrupted=Interrupted)
        self.assertEqual(await expr(), "caught")


    async def test_backward_jump_loop(self):
        """
        A tight counting loop sees the signal long before its bound
        """

        src = """
        (let [[n 0]]
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              (while (< n 50000000)
                (setq n (+ n 1)))
              n)
            ((e Interrupted) "caught")))
        """

        expr, _mod = self.compile_expr(src, Interrupted=Interrupted)
        self.assertEqual(await expr(), "caught")


    async def test_filter_reject_loop_python_predicate(self):
        """
        A native filter rejecting every element of a sync source through
        a Python predicate runs resume to resume with no instruction in
        between, and still sees the signal
        """

        def never(x):
            return False

        src = """
        (begin
          (import itertools)
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              (for-each [y (filter never (itertools.count))]
                y))
            ((e Interrupted) "caught")))
        """

        expr, _mod = self.compile_expr(
            src, Interrupted=Interrupted, never=never)
        self.assertEqual(await expr(), "caught")


    async def test_filter_reject_loop_driven_source(self):
        """
        A native filter with a None predicate over a driven source that
        yields only falsy elements loops resume to resume, and still
        sees the signal
        """

        src = """
        (begin
          (import itertools)
          (import operator)
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              (for-each [y (filter None
                                   (map operator.not_ (itertools.repeat 1)))]
                y))
            ((e Interrupted) "caught")))
        """

        expr, _mod = self.compile_expr(src, Interrupted=Interrupted)
        self.assertEqual(await expr(), "caught")


    async def test_deep_recursion_through_call(self):
        """
        Deep non-tail recursion through Call sees the signal
        """

        src = """
        (begin
          (import _thread)
          (define-global descend
            (lambda [n]
              (if (> n 0)
                  then: (+ 1 (descend (- n 1)))
                  else: 0)))
          (try
            (begin
              (_thread.interrupt_main)
              (descend 200000))
            ((e Interrupted) "caught")))
        """

        expr, _mod = self.compile_expr(src, Interrupted=Interrupted)
        self.assertEqual(await expr(), "caught")


# The end.
