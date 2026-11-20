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

Dynamic wind runs its before and after asides on extent entry and
exit. Either field of PUSH_WIND may be None, for no aside.
"""

import pytest

from koskari.lib import Code, OpCode
from tests import make_callable
from tests.lib.opcode import AsideRows, CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


def aside(*rows):
    """
    An aside over the namespace it is run in: rows and RETURN 1, for a
    field of PUSH_WIND
    """

    return AsideRows([*rows, ["RETURN", 1]])


class TestWindOpcodes(CevalTest):
    """
    Tests for PUSH_WIND, POP_WIND opcodes.
    """

    async def test_wind_return_from_nested_call_delivers_value_and_runs_after(self):
        """
        When body calls a helper that returns, unwind runs the after
        aside and delivers the return value to the wind's caller.

        Builds: PUSH_WIND (None, an after aside calling record),
        CALL ret99, RETURN 1. ret99 does CONST 99; RETURN 1. The ret99
        frame is a direct child of the wind; when it returns, unwind
        suspends, the after aside runs, then 99 must be delivered.
        """

        log = []

        def record_after():
            log.append('after')

        ret99_code, ret99_ns = build_expr([
            ['CONST', 99],
            ['RETURN', 1],
        ], name='ret99')
        ret99 = make_callable(ret99_code, ret99_ns)

        code, ns = build_expr([
            ['PUSH_WIND', (
                None, aside(['VAR', 'record'], ['CALL', 0]), 0, 0)],
            ['VAR', 'ret99'],
            ['CALL', 0],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ], values={
            'record': record_after,
            'ret99': ret99,
        })

        result = await make_callable(code, ns)()

        self.assertEqual(result, 99)
        self.assertEqual(log, ['after'])

    async def test_pop_wind_releases_body_result_when_after_thunk_raises(self):
        """
        An after aside whose call raises synchronously, a partial over a
        Python function here, leaves the wind's body result released
        once the exception propagates
        """

        import sys

        from koskari.lib import KosPartial

        def boom():
            raise RuntimeError("teardown")

        marker = object()
        code, ns = build_expr([
            ['PUSH_WIND', (
                None, aside(['VAR', 'boom'], ['CALL', 0]), 0, 0)],
            ['VAR', 'marker'],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ], values={'boom': KosPartial(boom), 'marker': marker})

        before = sys.getrefcount(marker)
        for _ in range(3):
            with self.assertRaises(RuntimeError):
                await make_callable(code, ns)()
        self.assertEqual(sys.getrefcount(marker), before)

    async def capture_continuation(self, log):
        """
        A delimited continuation whose remaining region records to log
        before it returns
        """

        from koskari.module import load_module_source_async, new_module

        src = '(var k (reset (begin (shift k k) (record "region"))))'
        mod = new_module("wind_cont")
        vars(mod)["__file__"] = "<wind_cont>"
        vars(mod)["record"] = log.append
        await load_module_source_async(mod, src, layout=False)
        return vars(mod)["k"]

    async def test_push_wind_drains_a_transfer_before_the_body(self):
        """
        A before aside that calls a continuation runs the transfer it
        makes before the wind body
        """

        log = []
        k = await self.capture_continuation(log)

        code, ns = build_expr([
            ['PUSH_WIND', (
                aside(['VAR', 'k'], ['CALL', 0]), None, 0, 0)],
            ['VAR', 'record'],
            ['CONST', "body"],
            ['CALL', 1],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ], values={'k': k, 'record': log.append})

        await make_callable(code, ns)()
        self.assertEqual(log, ["region", "body"])

    async def test_pop_wind_drains_a_transfer_before_continuing(self):
        """
        An after aside that calls a continuation runs the transfer it
        makes before the code after the wind
        """

        log = []
        k = await self.capture_continuation(log)

        code, ns = build_expr([
            ['PUSH_WIND', (
                None, aside(['VAR', 'k'], ['CALL', 0]), 0, 0)],
            ['CONST', None],
            ['POP_WIND', 0],
            ['VAR', 'record'],
            ['CONST', "after"],
            ['CALL', 1],
            ['RETURN', 1],
        ], values={'k': k, 'record': log.append})

        await make_callable(code, ns)()
        self.assertEqual(log, ["region", "after"])

    async def test_segments_run_over_the_namespace_live_at_the_push(self):
        """
        The before and after asides read and write the namespace the
        PUSH_WIND is written in: the before aside's store is seen by
        the body, whose 1 is added to the 11 the after aside leaves
        """

        before = aside(
            ['CONST', 1], ['VAR_STORE', 'x'], ['CONST', None])
        after = aside(
            ['VAR', 'x'], ['CONST', 10], ['OBJECT_ADD', 2],
            ['VAR_STORE', 'x'], ['CONST', None])
        code, ns = build_expr([
            ['PUSH_WIND', (before, after, 0, 0)],
            ['VAR', 'x'],
            ['POP_WIND', 0],
            ['VAR', 'x'],
            ['OBJECT_ADD', 2],
            ['RETURN', 1],
        ], values={'x': 0})

        self.assertEqual(12, await make_callable(code, ns)())

    async def test_wind_with_neither_aside_runs_its_body(self):
        code, ns = build_expr([
            ['PUSH_WIND', (None, None, 0, 0)],
            ['CONST', 5],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ])
        self.assertEqual(5, await make_callable(code, ns)())

    async def test_soften_reports_an_absent_aside_as_none(self):
        """
        Code.soften gives None for a field that carries no aside, and
        the softened rows build a Code that runs the same
        """

        log = []
        before = aside(['VAR', 'record'], ['CONST', 'before'], ['CALL', 1])
        code, ns = build_expr([
            ['PUSH_WIND', (before, None, 0, 0)],
            ['CONST', 3],
            ['POP_WIND', 0],
            ['RETURN', 1],
        ], values={'record': log.append})

        soft = code.soften()
        pushes = [
            arg[:2] for op, arg in soft
            if OpCode(op).name == "PUSH_WIND"]
        self.assertEqual(1, len(pushes))
        self.assertIsInstance(pushes[0][0], Code)
        self.assertIsNone(pushes[0][1])

        again = Code(code.namespace_spec, soft, "again")
        self.assertEqual(3, await make_callable(again, ns)())
        self.assertEqual(["before"], log)

    async def test_absent_aside_refused_outside_push_wind(self):
        code = [
            ["RESET", (0, None, 0, 0)],
            ["RETURN", 1],
        ]
        with self.assertRaises(TypeError) as caught:
            build_expr(code)
        self.assertEqual(
            "Code instruction 0 aside must be a Code, not NoneType",
            str(caught.exception))


# The end.
