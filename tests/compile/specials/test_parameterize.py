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
Tests for koskari.compile compile/parameterize special form.

Parameterize temporarily rebinds dynamic parameters for the duration of its
body. A consumer function (simulating bind-param) uses ParamRef(name).deref()
to read the current parameter value. When the body runs, it calls that
consumer; the consumer sees the parameterized value. When the block exits,
the parameter is restored.
"""


from koskari.lib import ParamRef

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestParameterizeSpecial(SpecialsTestCase):

    async def test_consumer_sees_parameterized_value(self):
        """
        parameterize sets format; consumer called inside body sees that value
        """

        def pretty(x):
            return ParamRef("format", "%r").deref()

        src_str = """
        (compile/parameterize [[format "woot %r woot"]]
          (compile/call pretty 100))
        """
        expr, mod = self.compile_expr(src_str, pretty=pretty)
        result = await expr()
        self.assertEqual(result, "woot %r woot")


    async def test_multiple_parameters_visible_to_consumer(self):
        """
        parameterize sets several params; consumer reads both via ParamRef
        """

        def check_both():
            return (ParamRef("a").deref(), ParamRef("b").deref())

        src_str = """
        (compile/parameterize [[a 100] [b 101]]
          (compile/call check_both))
        """
        expr, mod = self.compile_expr(src_str, check_both=check_both)
        result = await expr()
        self.assertEqual(result, (100, 101))


    async def test_parameter_restored_after_block_exits(self):
        """
        First call inside parameterize sees overridden value; second call
        outside sees default
        """

        def pretty(x):
            return ParamRef("format", "default").deref()

        def seq(*args):
            return args

        src_str = """
        (compile/call seq
          (compile/parameterize [[format "inner"]]
            (compile/call pretty 0))
          (compile/call pretty 1))
        """
        expr, mod = self.compile_expr(src_str, pretty=pretty, seq=seq)
        result = await expr()
        self.assertEqual(result, ("inner", "default"))


    async def test_multishot_applications_share_cells(self):
        """
        Multi-shot continuation applications share parameter cells: a store
        made in one application is visible in the next application, rather
        than each application restarting from the declared binding.

        Pins the Racket-cell semantics of
        design/interpreter/domain-task-isolation.md D4: PARAM node clones
        copy cell pointers, so every INSTALL re-enters the same cell.
        """

        def consume(i):
            ref = ParamRef("cellshare")
            seen = ref.deref()
            ref.store(f"app{i}")
            return (i, seen)

        def seq(*args):
            return args

        src_str = """
        (compile/let [[stolen 0]]
          (compile/call seq
            (compile/reset
              (compile/parameterize [[cellshare "declared"]]
                (compile/call consume
                  (compile/shift k (compile/setq stolen k) (compile/call k 0)))))
            (compile/call stolen 1)
            (compile/call stolen 2)))
        """

        expr, mod = self.compile_expr(src_str, consume=consume, seq=seq)
        result = await expr()
        self.assertEqual(
            result,
            ((0, "declared"), (1, "app0"), (2, "app1")))


# The end.
