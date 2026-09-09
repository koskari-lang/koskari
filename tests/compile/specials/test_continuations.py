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
Tests for continuation special forms and delimited transfer policy.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestDelimitedTransferNested(SpecialsTestCase):
    """
    A delimited invoke from inside a wind thunk that another delimited
    invoke is entering lands a second slice above the first; the inner
    invoke returns its reset's value to the thunk, and the outer invoke
    then finishes entering and runs its own slice.
    """

    async def test_nested_invoke_from_before_thunk_during_delimited_push(self):
        """
        dyn-wind wraps shift so the wind is captured with the prompt. The
        pass counter invokes k from the cloned wind's before-thunk exactly
        once, on the first re-entry, so the recursion is bounded: the inner
        invoke resumes the slice with 99 and returns 99 to the thunk, and
        the outer invoke resumes the slice with 0
        """

        src_str = """
        (let [[pass 0] [k nil]]
          (begin
            (setq k
              (reset
                (dyn-wind
                  (begin
                    (setq pass (+ pass 1))
                    (cond
                      ((== pass 2) (call k 99))
                      (else: nil)))
                  (shift k k)
                  nil)))
            (call record (call k 0))
            pass))
        """
        results = []
        expr, mod = self.compile_expr(src_str, record=results.append)
        passes = await expr()
        self.assertEqual(results, [0])
        self.assertEqual(passes, 3)


    async def test_unbounded_nested_invoke_raises_recursion_error(self):
        """
        every re-entry of the cloned before-thunk invokes k again, so
        each pass lands another slice above the last. Each landing adds
        frames, so the recursion limit refuses an invoke once the chain
        is as deep as sys.getrecursionlimit() and RecursionError raises
        at that invoke site instead of the chain growing without bound
        """

        src_str = """
        (let [[pass 0] [k nil]]
          (begin
            (setq k
              (reset
                (dyn-wind
                  (begin
                    (setq pass (+ pass 1))
                    (cond
                      ((> pass 1) (call k 99))
                      (else: nil)))
                  (shift k k)
                  nil)))
            (call k 0)))
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(RecursionError):
            await expr()


# The end.
