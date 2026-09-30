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
tests.lib.opcode.test_build_kwds_names

BUILD_KWDS_NAMES pops its values into a dict under the names it
carries. A failed insert leaves the popped values owned by the stack
for the unwind to release once.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestBuildKwdsNames(CevalTest):

    async def test_builds_the_dict(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["BUILD_KWDS_NAMES", ("a", "b")],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), {"a": 1, "b": 2})

    async def test_failed_insert_releases_each_value_once(self):
        """
        An unhashable name fails the insert; the values already on the
        stack are released by the unwind alone, so a held value's
        refcount returns to where it started
        """

        held = object()
        before = sys.getrefcount(held)
        code, ns = build_expr([
            ["CONST", held],
            ["CONST", 2],
            ["BUILD_KWDS_NAMES", ("a", ["unhashable"])],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError):
            await fn()
        del fn, code, ns
        self.assertEqual(sys.getrefcount(held), before)


# The end.
