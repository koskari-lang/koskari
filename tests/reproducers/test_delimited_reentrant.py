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
Re-entrant delimited continuations: a shift firing inside an invoked k.

Danvy/Filinski shift reifies k = lambda x. <E[x]> -- the captured
continuation carries its own delimiter, so every invocation of k runs
the region under a fresh prompt instance. A shift inside that run
captures up to the fresh instance, terminates only that invocation, and
delivers its value at the invoke site. Before prompt instance identity
(design/interpreter/prompt-instance-identity.md) the machinery resolved
prompts by chain position, so the inner shift unwound to the ORIGINAL
prompt and discarded the remainder of the suspended outer handler --
Felleisen control semantics where shift was promised.

The function-valued amb cases are the acceptance shapes: amb is a
callee-hosted shift whose handler invokes k once per choice, so nesting
two ambs demands a multi-shot shift inside each invocation of another
multi-shot shift, and the collected results must be the full product.

Note these shapes are order-sensitive under compile_expr: define-global
bindings do not persist across a fresh module per test, so running this
file alone yields extra NameErrors that the phased runner does not show.
The VM behaviour is the same either way; only the binding lookup differs.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


class TestDelimitedReentrant(SpecialsTestCase):

    async def test_multishot_with_second_shift(self):
        """
        Outer k invoked twice; its continuation contains a second shift.
        Each invocation re-delimits, so the inner shift terminates only
        that invocation: (k 1) = 11, (k 2) = 12, handler total 23.
        """

        src = """
        (reset
          (+ (shift k (+ (call k 1) (call k 2)))
             (shift j (call j 10))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 23)


    async def test_inner_abandon_delivers_to_invoke_site(self):
        """
        The inner shift abandons (never invokes its own k); its body
        value becomes the value of the enclosing k-invocation, and the
        outer handler continues
        """

        src = """
        (reset
          (+ (shift k (+ (call k 1) (call k 2)))
             (shift j 100)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 200)


    async def test_k_applied_to_own_result(self):
        """
        Three-deep re-entrancy: (k (k (k 0))) threads each result back
        through the region
        """

        src = """
        (reset
          (+ 1 (shift k
                 (call k (call k (call k 0))))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 3)


    async def test_amb_product(self):
        """
        Function-valued amb nested in another: the collected results are
        the full cross product, in choice order
        """

        src = """
        (begin
          (define-global amb (lambda [choices]
            (shift k
              (let [[out (#list)]]
                (for-each [c choices]
                  (call (attr out extend)
                    (call k c)))
                out))))
          (reset
            (#list
              (#tuple (call amb (#list 1 2))
                              (call amb (#list 10 20))))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(),
                         [(1, 10), (1, 20), (2, 10), (2, 20)])


    async def test_amb_with_require_prunes(self):
        """
        amb with a failing guard: fail abandons the branch by returning
        an empty list, so only passing choices survive
        """

        src = """
        (begin
          (define-global amb (lambda [choices]
            (shift k
              (let [[out (#list)]]
                (for-each [c choices]
                  (call (attr out extend)
                    (call k c)))
                out))))
          (define-global fail (lambda []
            (shift k (#list))))
          (reset
            (#list
              (let [[x (call amb (#list 1 2 3 4))]]
                (cond
                  ((> x 2) x)
                  (else: (call fail)))))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), [3, 4])

# The end.
