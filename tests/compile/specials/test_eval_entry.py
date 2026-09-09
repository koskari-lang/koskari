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
The evaluator's entry refuses FAST-slot code in a namespace without fast
slots, and that refusal leaves an enclosing evaluation untouched

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import ModuleNamespace, parameter
from tests.lib.opcode import build_code

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def fast_code_in_module_namespace():
    """
    An expression that will be refused at entry: a Code addressing a fast
    slot, bound to a module namespace, which has none
    """

    code = build_code([["FAST", 0], ["RETURN", 1]], local_syms=("x",))
    return code.expression(ModuleNamespace("foreign", {}))


class TestFastPrecheck(SpecialsTestCase):

    async def test_fast_code_in_module_namespace_is_refused(self):
        expr = fast_code_in_module_namespace()
        with self.assertRaises(RuntimeError) as caught:
            expr().send(None)
        self.assertIn("fast slots", str(caught.exception))

    async def test_refused_entry_leaves_the_enclosing_eval_current(self):
        """
        A refused entry from Python code running inside an evaluation
        must not pop that evaluation's thread-local entries: a parameter
        bound by the enclosing evaluation reads the same before and after
        """

        seen = []

        def probe():
            seen.append(parameter("depth", "unbound"))
            try:
                fast_code_in_module_namespace()().send(None)
            except RuntimeError:
                pass
            seen.append(parameter("depth", "unbound"))
            return len(seen)

        src_str = """
        (parameterize [[depth 7]]
          (call probe))
        """
        expr, mod = self.compile_expr(src_str, probe=probe)
        self.assertEqual(await expr(), 2)
        self.assertEqual(seen, [7, 7])

    async def test_repeated_refusals_do_not_unwind_past_the_enclosing_eval(self):
        seen = []

        def probe():
            for _ in range(3):
                try:
                    fast_code_in_module_namespace()().send(None)
                except RuntimeError:
                    pass
                seen.append(parameter("depth", "unbound"))
            return "done"

        src_str = """
        (parameterize [[depth 7]]
          (call probe)
          (parameter depth))
        """
        expr, mod = self.compile_expr(src_str, probe=probe)
        self.assertEqual(await expr(), 7)
        self.assertEqual(seen, [7, 7, 7])


# The end.
