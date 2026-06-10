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
Tests for koskari.compile compile/dyn-with special form.

Multi-shot context manager: enter runs before body, exit runs after (or on
exception). Uses PUSH_DYN_WITH/POP_DYN_WITH. Participates in transfer plans.
"""

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class SimpleMgr:
    """
    Python context manager for tests; tracks enter/exit
    """

    def __init__(self, value=42):
        self.value = value
        self.entered = False
        self.exited = False
        self.exc_info = (None, None, None)


    def __enter__(self):
        self.entered = True
        return self.value


    def __exit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return False


class TestDynWithSpecial(SpecialsTestCase):
    """
    Tests for compile/dyn-with special form
    """

    async def test_normal_enter_exit_expression_returns_none(self):
        """
        dyn-with runs enter, body, exit; expression result is None
        """

        state = {}

        def set_y(val):
            state["y"] = val

        mgr = SimpleMgr(100)
        src = """
        (compile/dyn-with [(compile/call make_mgr)]
          (compile/call set_y 1))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, set_y=set_y)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("y"), 1)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_dyn_with_binding_entered_value(self):
        """
        dyn-with [name expr] binds the enter result to name in body
        """

        state = {}
        mgr = SimpleMgr(200)

        def check(x):
            state["got"] = x

        src = """
        (compile/dyn-with [x (compile/call make_mgr)]
          (compile/call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("got"), 200)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_emits_push_dyn_with_pop_dyn_with(self):
        """
        compile/dyn-with emits PUSH_DYN_WITH and POP_DYN_WITH opcodes
        """

        from koskari.lib import OpCode

        src = "(compile/dyn-with [x (compile/call make_mgr)] (compile/call noop x))"
        code = self.compile_to_code(src, make_mgr=lambda: None, noop=lambda x: None)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.KOS_OP_PUSH_DYN_WITH, ops)
        self.assertIn(OpCode.KOS_OP_POP_DYN_WITH, ops)


    async def test_dyn_with_body_value_is_expression_result(self):
        """
        dyn-with block evaluates to the body's final value (TDD: will fail until
        POP_DYN_WITH preserves body value instead of pushing None)
        """

        mgr = SimpleMgr(100)
        src = """
        (compile/dyn-with [x (compile/call make_mgr)]
          100)
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 100)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_dyn_with_return_from_nested_call_runs_exit_and_delivers_value(self):
        """
        When body calls a function that returns, exit runs and value is delivered
        """

        mgr = SimpleMgr(200)
        src = """
        (compile/dyn-with [x (compile/call make_mgr)]
          (compile/call (compile/lambda [] 99)))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


class TestDynWithTransferWindReproducer(SpecialsTestCase):
    """
    Minimal reproducer: continuation invoke into a dyn-with body should
    re-run __enter__ as part of the transfer plan wind step.
    """

    async def test_enter_runs_on_transfer_into_dyn_with_body(self):
        """
        capture/cc inside dyn-with body; gated invoke_saved after setup.
        Transfer into the captured segment must run enter again.
        """

        log = []
        enter_count = [0]
        saved = []

        def record(s):
            log.append(s)
            return s

        def make_mgr():
            mgr = SimpleMgr(42)
            orig_enter = mgr.__enter__

            def counting_enter():
                record("enter-%s" % enter_count[0])
                enter_count[0] += 1
                return orig_enter()

            mgr.__enter__ = counting_enter
            return mgr

        def stash(k):
            record("stash-py")
            saved.append(k)

        def invoke_saved():
            record("invoke-saved-py saved=%s" % bool(saved))
            k = saved[0]
            saved.clear()
            return k(99)

        src_str = """
        (compile/begin
          (compile/define result
            (compile/dyn-with [x (compile/call make_mgr)]
              (compile/capture/cc k
                (compile/begin
                  (compile/call record "stash")
                  (compile/call stash k)
                  (compile/call record "capture-body")
                  42))))
          (compile/call record "after-setup")
          (compile/call record result)
          (compile/cond [saved (invoke_saved)] [else: result]))
        """
        expr, mod = self.compile_expr(
            src_str,
            make_mgr=make_mgr,
            stash=stash,
            invoke_saved=invoke_saved,
            record=record,
            saved=saved)
        await expr()
        self.assertGreaterEqual(
            enter_count[0], 2,
            msg="log=%r enter_count=%r" % (log, enter_count[0]))
        self.assertTrue(
            any(isinstance(entry, str) and "invoke-saved-py" in entry
                for entry in log),
            msg="log=%r" % (log,))


# The end.
