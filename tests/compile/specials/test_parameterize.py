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


    async def test_parameter_unbound_again_after_block_exits(self):
        """
        First call inside parameterize sees the overridden value; after
        the extent pops the name is unbound again, which a ref with no
        default reports as NameError. Asserting the absence rather than a
        declared default keeps a stubbed-out restore from passing.
        """

        def probe():
            try:
                return ParamRef("format").deref()
            except NameError:
                return "unbound"

        def seq(*args):
            return args

        src_str = """
        (compile/call seq
          (compile/parameterize [[format "inner"]]
            (compile/call probe))
          (compile/call probe))
        """
        expr, mod = self.compile_expr(src_str, probe=probe, seq=seq)
        result = await expr()
        self.assertEqual(result, ("inner", "unbound"))


    async def test_extent_cell_is_not_the_root_cell(self):
        """
        parameterize shadows rather than overwrites: a value already in
        the root scope is intact underneath the extent, and the extent's
        binding never reaches the root scope.
        """

        from koskari.lib import Domain

        seen = []

        def outer_store():
            ParamRef("shadowed").store("root-value")

        def inside():
            seen.append(Domain.current_domain().param_scopes())
            return ParamRef("shadowed").deref()

        def seq(*args):
            return args

        src_str = """
        (compile/call seq
          (compile/call outer_store)
          (compile/parameterize [[shadowed "extent-value"]]
            (compile/call inside))
          (compile/call inside))
        """
        expr, mod = self.compile_expr(
            src_str, outer_store=outer_store, inside=inside, seq=seq)
        result = await expr()
        self.assertEqual(result, (None, "extent-value", "root-value"))

        inner_scopes = seen[0]
        self.assertEqual(inner_scopes["shadow"]["shadowed"].deref(),
                         "extent-value")
        self.assertEqual(inner_scopes["root"]["shadowed"].deref(),
                         "root-value")


    async def test_multishot_applications_share_cells(self):
        """
        Multi-shot continuation applications share parameter cells: a store
        made in one application is visible in the next application, rather
        than each application restarting from the declared binding.

        Pins the Racket-cell semantics of
        design/archive/domain-task-isolation.md D4: PARAM node clones
        copy cell pointers, so every INSTALL re-enters the same cell.
        """

        def consume(i):
            cell = ParamRef("cellshare")
            seen = cell.deref()
            cell.store(f"app{i}")
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
