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
Tests for PUSH_WIND, POP_WIND opcodes.

Dynamic wind runs before/after thunks on extent entry and exit.
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestWindOpcodes(CevalTest):
    """
    Tests for PUSH_WIND, POP_WIND opcodes.
    """

    async def test_wind_return_from_nested_call_delivers_value_and_runs_after(self):
        """
        When body calls a helper that returns, unwind runs after-thunk and
        delivers the return value to the wind's caller.

        Builds: PUSH_WIND 2 (before=nil, after=thunk), CALL ret99, RETURN 1.
        ret99 does CONST 99; RETURN 1. The ret99 frame is a direct child of
        the wind; when it returns, unwind suspends, after-thunk runs, then
        99 must be delivered.
        """

        log = []

        def record_after():
            log.append('after')

        after_code, after_ns = build_expr([
            ['VAR', 'record'],
            ['CALL', 0],
            ['CONST', None],
            ['RETURN', 1],
        ], values={'record': record_after}, name='after_thunk')
        after_thunk = make_callable(after_code, after_ns)

        ret99_code, ret99_ns = build_expr([
            ['CONST', 99],
            ['RETURN', 1],
        ], name='ret99')
        ret99 = make_callable(ret99_code, ret99_ns)

        code, ns = build_expr([
            ['CONST', None],
            ['VAR', 'after_thunk'],
            ['PUSH_WIND', 2],
            ['VAR', 'ret99'],
            ['CALL', 0],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ], values={
            'after_thunk': after_thunk,
            'ret99': ret99,
        })

        result = await make_callable(code, ns)()

        self.assertEqual(result, 99)
        self.assertEqual(log, ['after'])

    async def test_pop_wind_releases_body_result_when_after_thunk_raises(self):
        """
        An after thunk whose call raises synchronously, a partial over a
        Python function here, leaves the wind's body result released
        once the exception propagates
        """

        import sys
        from koskari.lib import KosPartial

        def boom():
            raise RuntimeError("teardown")

        marker = object()
        code, ns = build_expr([
            ['CONST', None],
            ['VAR', 'boom'],
            ['PUSH_WIND', 2],
            ['VAR', 'marker'],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ], values={'boom': KosPartial(boom), 'marker': marker})

        before = sys.getrefcount(marker)
        for _ in range(3):
            with self.assertRaises(RuntimeError):
                await make_callable(code, ns)()
        self.assertEqual(sys.getrefcount(marker), before)


# The end.
