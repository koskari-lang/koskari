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
tests.reproducers.test_inplace_invoke_operands

The first invoke of a delimited continuation runs in place on the live
chain. The slice resumes over the region's own pending operands, the
handler's pending operands wait on the shift node for the invoke to
return, and a region node between the reset and the shift that the
slice pops on its way out takes nothing the handler still needs.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


class _CM:
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class TestInplaceInvokeOperands(SpecialsTestCase):

    async def evaluate(self, src, **defns):
        expr, _mod = self.compile_expr(
            src, add=lambda a, b: a + b, sub=lambda a, b: a - b,
            mul=lambda a, b: a * b, tuple=tuple, cm=_CM, **defns)
        return await expr()


    async def test_handler_operand_pending_around_the_invoke(self):
        src = """
        (reset (call add 1 (shift k (call mul 2 (call k 5)))))
        """
        self.assertEqual(12, await self.evaluate(src))


    async def test_handler_operand_order_is_kept(self):
        src = """
        (reset (call add 1 (shift k (call sub 100 (call k 5)))))
        """
        self.assertEqual(94, await self.evaluate(src))


    async def test_region_and_handler_operands_do_not_mix(self):
        src = """
        (reset (call tuple #["a" (shift k (call tuple #["b" (call k 5)]))]))
        """
        self.assertEqual(("b", ("a", 5)), await self.evaluate(src))


    async def test_try_between_reset_and_shift(self):
        src = """
        (reset (try (call add 1 (shift k (call mul 2 (call k 5))))
                 [finally: None]))
        """
        self.assertEqual(12, await self.evaluate(src))


    async def test_with_between_reset_and_shift(self):
        src = """
        (reset (with [(call cm)]
                 (call add 1 (shift k (call mul 2 (call k 5))))))
        """
        self.assertEqual(12, await self.evaluate(src))


    async def test_for_each_between_reset_and_shift(self):
        src = """
        (reset (for-each [i #[0]]
                 (break (call add 1 (shift k (call mul 2 (call k 5)))))))
        """
        self.assertEqual(12, await self.evaluate(src))


    async def test_second_invoke_takes_the_graft_path(self):
        src = """
        (reset (call add 1 (shift k (call add (call k 5) (call k 10)))))
        """
        self.assertEqual(17, await self.evaluate(src))


    async def test_escape_from_the_resumed_slice_abandons_the_handler(self):
        """
        A let/cc escape captured in the region and invoked from inside
        the resumed slice delivers to the let/cc point: the shift
        handler's pending work is abandoned with the stashed span
        """

        src = """
        (reset
          (call add 100
            (let/cc esc
              (call add 1 (shift d (call add 1000 (call d 1))))
              (call esc 5))))
        """
        self.assertEqual(105, await self.evaluate(src))


    async def test_handler_that_never_invokes(self):
        src = """
        (reset (call add 1 (shift k 42)))
        """
        self.assertEqual(42, await self.evaluate(src))



class TestCaptureInsideResumedSlice(SpecialsTestCase):
    """
    An undelimited continuation captured inside a slice an in-place
    invoke resumed carries the shift handler's pending work: re-entering
    it after the region has finished runs the slice to its end, then the
    handler's remainder, and delivers where the region's value went
    """

    async def test_reentry_replays_the_handler_remainder(self):
        src = """
        ((lambda []
          (define st (call make_state))
          (define r (reset
                      (call add 100
                            (shift d (call add 1000 (call d 1)))
                            (call/cc (lambda [k] (call stash st k) 0)))))
          (call note st r)
          (cond [(call once st) ((call saved st) 5)])
          r))
        """

        class State:
            def __init__(self):
                self.k = None
                self.seen = []
                self.fired = False

        def once(st):
            if st.fired:
                return False
            st.fired = True
            return True

        expr, _mod = self.compile_expr(
            src, add=lambda *a: sum(a),
            make_state=State,
            stash=lambda st, k: setattr(st, "k", k),
            saved=lambda st: st.k,
            note=lambda st, r: st.seen.append(r),
            once=once)
        result = await expr()
        self.assertEqual(1106, result)


class TestCaptureLeavesNoCallee(SpecialsTestCase):
    """
    call/cc sets its callee aside for the capture, so a continuation
    invoked in argument position delivers its value where the call/cc
    form stood and the earlier operands are what they were
    """

    async def test_invoke_in_argument_position(self):
        src = """
        (call g 1 (call/cc (lambda [k] (call k 10) 99)) 3)
        """
        expr, _mod = self.compile_expr(src, g=lambda *a: a)
        self.assertEqual((1, 10, 3), await expr())


# The end.
