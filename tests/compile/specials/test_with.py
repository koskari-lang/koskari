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
Tests for koskari.compile compile/with special form.

Single-shot context manager: enter runs before body, exit runs after (or on
exception). Uses PUSH_WITH/POP_WITH; sync __enter__/__exit__ only.
"""


from . import SpecialsTestCase


class SimpleMgr:
    """Python context manager for tests; tracks enter/exit and optional binding."""

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


class SwallowMgr(SimpleMgr):
    """Context manager that swallows exceptions (exit returns True)."""

    def __exit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return True


class TestWithSpecial(SpecialsTestCase):

    def test_normal_enter_exit_expression_returns_none(self):
        """With runs enter, body, exit; expression result is None."""
        state = {}

        def set_y(val):
            state["y"] = val

        mgr = SimpleMgr(100)
        src = """
        (compile/with [(compile/call make_mgr)]
          (compile/call set_y 1))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, set_y=set_y)
        result = expr().run()
        self.assertIsNone(result)
        self.assertEqual(state.get("y"), 1)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)

    def test_with_binding_entered_value(self):
        """With [name expr] binds the enter result to name in body."""
        state = {}
        mgr = SimpleMgr(200)

        def check(x):
            state["got"] = x

        src = """
        (compile/with [x (compile/call make_mgr)]
          (compile/call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = expr().run()
        self.assertIsNone(result)
        self.assertEqual(state.get("got"), 200)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)

    def test_exception_propagates_when_exit_does_not_swallow(self):
        """Exception in body propagates after exit when exit returns false."""
        mgr = SimpleMgr()

        def raise_err():
            raise ValueError("oops")

        src = """
        (compile/with [(compile/call make_mgr)]
          (compile/call raise_err))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, raise_err=raise_err)
        with self.assertRaises(ValueError) as ctx:
            expr().run()
        self.assertIn("oops", str(ctx.exception))
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)

    def test_exception_swallowed_when_exit_returns_true(self):
        """
        When exit returns true, exception is swallowed; expression yields None.
        Skipped: resume-after-swallow requires setting PC to after POP_WITH
        and try_local_exception_handler to continue; not yet implemented.
        """
        self.skipTest("resume after with-exit swallow not yet implemented")


# The end.
