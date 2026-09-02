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

The evaluator checks for pending signals on a counter of executed
instructions, so a signal raised while Koskari code runs interrupts it
even when the code makes no call, jump, return, or yield between the
signal and its check. Each test arms the interrupt from inside the
running code and then runs a stretch with no other reason to poll: a
long straight line of stores, a tight counting loop, and a deep
non-tail recursion. The Koskari code catches the interrupt itself, so
a signal the evaluator failed to poll would surface only after the
stretch completed, on return to Python, and fail the test.

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


    async def test_straight_line(self):
        """
        A straight-line stretch longer than the poll cadence, with no
        call, jump, return, or yield in it, still sees the signal
        """

        stores = " ".join("(setq x 0)" for _ in range(3000))
        src = f"""
        (let [[x 1]]
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              {stores}
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
