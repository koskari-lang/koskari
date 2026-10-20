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
raises the interrupt from inside the running code and then runs such a
stretch: a tight counting loop, a tail-recursive loop, and a deep
non-tail recursion. The Koskari code catches the interrupt itself, so
a signal the evaluator failed to poll would
surface only after the stretch completed, on return to Python, and
fail the test. A straight line with no cooperation point in it runs to
its end first; the signal is then taken at the next point after it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import signal

import pytest

from tests.compile.specials import SpecialsTestCase


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
        its end; the signal is taken at the first back-edge after it.
        Each store counts itself, and the count is read from the
        handler, so the stretch is proven to have completed before the
        signal was taken
        """

        stores = " ".join("(setq x (+ x 1))" for _ in range(3000))
        src = f"""
        (let [[x 0] [n 0]]
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              {stores}
              (while (< n 1)
                (setq n (+ n 1)))
              "ran off the end")
            ((e Interrupted) x)))
        """

        expr, _mod = self.compile_expr(src, Interrupted=Interrupted)
        self.assertEqual(await expr(), 3000)


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


    async def test_match_repeat_loop(self):
        """
        A match whose REPEAT passes call nothing sees the signal at a
        pass's end, long before the subject runs out
        """

        src = """
        (let [[n 0]]
          (import _thread)
          (try
            (begin
              (_thread.interrupt_main)
              (match subject
                [[#[(REPEAT (TEST_ANY))]] "ran off the end"]))
            ((e Interrupted) "caught")))
        """

        expr, _mod = self.compile_expr(
            src, Interrupted=Interrupted, subject=(0,) * 200000)
        self.assertEqual(await expr(), "caught")


    async def test_tail_call_inside_let(self):
        """
        A tail-recursive loop whose call sits inside a let has no
        back-edge and its argument window lives on the let's segment,
        so the call core's tail arm declines it and the install runs
        through the call slot; that install is still a cooperation
        point. The signal is raised from a C builtin partway through, so
        no Python frame can take it first
        """

        src = """
        (begin
          (import _thread)
          (define-global spin
            (lambda [n]
              (let [[m (+ n 1)]]
                (depths.append m)
                (cond ((== m 500) (_thread.interrupt_main)))
                (cond ((< m 100000) (spin m)) (else: m)))))
          (try
            (spin 0)
            ((e Interrupted) "caught")))
        """

        depths = []
        expr, _mod = self.compile_expr(
            src, Interrupted=Interrupted, depths=depths)
        self.assertEqual(await expr(), "caught")
        self.assertEqual(depths[-1], 500)


    async def test_deep_recursion_through_call(self):
        """
        Deep non-tail recursion through Call sees the signal. The
        signal is raised from a C builtin at depth 500, so the install
        that takes it is the five hundred and first, not the first
        """

        src = """
        (begin
          (import _thread)
          (define-global descend
            (lambda [n]
              (depths.append n)
              (cond ((== n 500) (_thread.interrupt_main)))
              (cond
                ((< n 200000) (+ 1 (descend (+ n 1))))
                (else: 0))))
          (try
            (descend 0)
            ((e Interrupted) "caught")))
        """

        depths = []
        expr, _mod = self.compile_expr(
            src, Interrupted=Interrupted, depths=depths)
        self.assertEqual(await expr(), "caught")
        self.assertEqual(depths[-1], 500)


# The end.
