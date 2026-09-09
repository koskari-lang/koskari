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


class TestDelimitedTransferNestedForbid(SpecialsTestCase):
    """
    Delimited PUSH transfer plans must not accept nested continuation invoke;
    fail fast with RuntimeError instead of silent wrong-topology replan.
    """

    async def test_nested_invoke_during_delimited_push_transfer_raises(self):
        """
        While a delimited PUSH transfer is draining, a dyn-wind before-thunk on
        the captured segment invokes the same continuation again.  That nested
        invoke must raise RuntimeError and abandon the transfer nest.

        dyn-wind wraps shift so heritage nodes are captured with the prompt.
        The pass counter skips nested invoke on the initial before entry and
        performs it only when the cloned wind before-thunk runs during (k 0)
        PUSH wind.
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
                      ((>= pass 2) (call k 99))
                      (else: nil)))
                  (shift k k)
                  nil)))
            (call k 0)))
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(RuntimeError) as ctx:
            await expr()
        self.assertIn("delimited transfer", str(ctx.exception))


# The end.
