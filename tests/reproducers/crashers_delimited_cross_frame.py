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
Memory-unsafe delimited-capture shapes. NOT COLLECTED BY DEFAULT.

These two cases SIGSEGV. A segfault kills the pytest process, taking every
other test in the run with it, so this module is deliberately named
outside the default test_*.py collection pattern. It is not skipped and
not xfailed -- the assertions state the intended result, exactly like the
sibling reproducers -- it is quarantined only because the failure mode is
a crash rather than a failed assertion.

Run it deliberately (an explicitly named file is collected regardless of
the filename pattern), expecting the process to die:

    tox -qe py -- tests/reproducers/crashers_delimited_cross_frame.py

Both shapes are cross-frame capture: the shift is not in the reset's own
code object. Under the CLI driver the same programs surface as
"SystemError: program counter out of bounds" and as a silent exit with no
value and no error; under pytest both are SIGSEGV. Treat the crash as the
authoritative symptom -- the CLI outcomes are the same fault reaching a
different host.

The second case is the shape a function-valued amb needs, which is why a
delimited amb is not expressible today and a call/cc one is.

Companion assertion-level cases: test_delimited_cross_frame.py
Companion operand-stack cases: test_delimited_stack_discipline.py

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


class TestDelimitedCrossFrameCrashers(SpecialsTestCase):

    async def test_shift_is_entire_callee_body(self):
        """
        Prompt in the caller, shift is the callee's whole body. SIGSEGV.
        """

        src = """
        (compile/let [[f (compile/lambda []
                           (compile/shift k (compile/call k 1)))]]
          (compile/reset (compile/call f)))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1)


    async def test_multishot_from_callee_collects_each_choice(self):
        """
        k invoked once per choice from a callee -- the shape a function
        form of amb needs. SIGSEGV.
        """

        src = """
        (compile/let [[collect
                       (compile/lambda [choices]
                         (compile/shift k
                           (compile/let [[out (compile/#list)]]
                             (compile/for-each [c choices]
                               (compile/call (compile/attr out append)
                                 (compile/call k c)))
                             out)))]]
          (compile/reset
            (+ 100 (compile/call collect (compile/#list 1 2 3)))))
        """

        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), [101, 102, 103])

# The end.
