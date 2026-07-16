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
test_nested_asyncgen_gc_traverse - reproducer for nested async-gen GC traverse crash

This test drives a Koskari async generator nested inside a koskari for-each
loop and forces garbage collection at yield boundaries. The intent is to
trigger GC traversal of a frozen generator chain while the VM is mid-drive.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import gc

import pytest

import koskari.compile.specials  # noqa: F401
from tests.dis.dis import dis
from tests.compile.specials import SpecialsTestCase

pytestmark = pytest.mark.phase_reproducers


def _collect_with_forced_gc(agen):
    out = []
    while True:
        try:
            out.append(next(agen))
        except StopIteration:
            break
        gc.collect()
    return out


class TestNestedAsyncGenGcTraverseReproducer(SpecialsTestCase):

    async def test_nested_foreach_over_koskari_asyncgen_survives_gc(self):
        """
        Reproducer: outer foreach iterates a Koskari async gen and yields values.

        Without the GC-safety fix, this can crash in CPython GC traversal
        (visit_decref) while traversing the frozen dynchain of the inner gen.
        """

        src = """
        (lambda []
          (define mk []
            (yield 1)
            (yield 2)
            (yield 3))

          (for-each [x (mk)]
            (yield x)))
        """

        print("src", src)
        expr, _mod = self.compile_expr(src)
        dis(expr)

        fn = await expr()
        dis(fn)

        got = _collect_with_forced_gc(iter(fn()))

        self.assertEqual(got, [1, 2, 3])


# The end.
