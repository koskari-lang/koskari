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
A shift handler in a callee frame that hands k to a helper, which invokes
it. Chain order root..top at the invoke is caller FRAME, PROMPT, callee
FRAME, SHIFT, helper FRAME: the handler's frame and the invoking frame
are two different frames, and the slice frees the handler's frame while
the helper's survives in the stashed span. The in-place invoke has to
snapshot the handler's frame, not the invoking one.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


class TestDelimitedHandlerHelper(SpecialsTestCase):

    async def test_helper_invokes_k_once(self):
        """
        The helper's result flows into the handler's remainder, and the
        handler's value becomes the reset's value
        """

        src = """
        (begin
          (define-global invoker (lambda [k] (+ (call k 1) 100)))
          (define-global inner (lambda []
                                  (shift k (+ (call invoker k) 1000))))
          (reset (call inner)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1101)


    async def test_helper_tail_runs_once(self):
        """
        The helper's code after the invoke runs exactly once, in the
        helper's own frame, and the handler's code after the helper call
        runs exactly once
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        src = """
        (begin
          (define-global invoker (lambda [k]
                                    (let [[v (call k 2)]]
                                      (rec "helper-tail")
                                      (* v 10))))
          (define-global inner (lambda [n]
                                  (+ n (shift k
                                         (let [[w (call invoker k)]]
                                           (rec "handler-tail")
                                           (+ w 5))))))
          (reset (+ 100 (call inner 1))))
        """

        expr, _mod = self.compile_expr(src, rec=rec)
        self.assertEqual(await expr(), 1035)
        self.assertEqual(events, ["helper-tail", "handler-tail"])


    async def test_helper_two_frames_deep(self):
        """
        Two helper frames between the invoke and the shift
        """

        src = """
        (begin
          (define-global invoker (lambda [k] (+ (call k 1) 100)))
          (define-global relay (lambda [k] (+ (call invoker k) 10)))
          (define-global inner (lambda []
                                  (shift k (+ (call relay k) 1000))))
          (reset (call inner)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1111)


    async def test_helper_multishot(self):
        """
        The helper invokes k twice, each invoke attaching its own copy,
        and the handler sums them
        """

        src = """
        (begin
          (define-global twice (lambda [k] (+ (call k 1) (call k 2))))
          (define-global inner (lambda []
                                  (+ 10 (shift k (call twice k)))))
          (reset (call inner)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 23)

# The end.
