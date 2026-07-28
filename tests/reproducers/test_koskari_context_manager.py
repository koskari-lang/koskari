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
tests.reproducers.test_koskari_context_manager

Full-stack reproducers for koskari-defined context managers driven by the
with special form: the synchronous protocol with @demand-decorated
__enter__/__exit__, and the asynchronous protocol with undecorated
__aenter__/__aexit__.

Every shape runs in a child process. A with block over a koskari-defined
manager can wedge the evaluator inside its C dispatch loop, which no
asyncio timeout and no signal reach; the parent kills a wedged child and
reports it as a failure instead of hanging the suite.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import json
import os
import subprocess
import sys
import unittest

import pytest

from tests.reproducers.context_manager_runner import RESULT_PREFIX


pytestmark = pytest.mark.phase_reproducers

_RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
        'context_manager_runner.py')

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))

# generous; a healthy shape finishes in well under a second
_TIMEOUT = 20


class ContextManagerShapeTestCase(unittest.TestCase):
    """
    Drives one named shape per test through a child process
    """

    def run_shape(self, shape):
        """
        Run a shape in a child process and return its result mapping.
        Fails the test if the child wedges, dies, or prints no result.
        """

        cmd = [sys.executable, _RUNNER, shape]

        try:
            proc = subprocess.run(
                cmd, cwd=_ROOT, capture_output=True, text=True,
                timeout=_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.fail(f"shape {shape!r} wedged: no result after"
                      f" {_TIMEOUT}s, child killed")

        for line in proc.stdout.splitlines():
            if line.startswith(RESULT_PREFIX):
                return json.loads(line[len(RESULT_PREFIX):])

        self.fail(f"shape {shape!r} produced no result line;"
                  f" exit {proc.returncode}, stderr:\n{proc.stderr}")


    def assertManagers(self, result, *expected):
        """
        Check each manager's recorded entered/exited/exc_type against an
        (entered, exited, exc_type) triple
        """

        states = result['managers']
        self.assertEqual(len(states), len(expected))

        for index, (state, want) in enumerate(zip(states, expected)):
            entered, exited, exc_type = want
            self.assertEqual(state['entered'], entered,
                             f"manager {index} entered")
            self.assertEqual(state['exited'], exited,
                             f"manager {index} exited")
            self.assertEqual(state['exc_type'], exc_type,
                             f"manager {index} exc_type")


class TestDirectProtocol(ContextManagerShapeTestCase):
    """
    The protocol methods called directly, with no with block involved.
    Establishes that @demand on __enter__/__exit__ produces plain values
    and that the async methods await to plain values.
    """

    def test_sync_protocol_methods(self):
        result = self.run_shape('direct_protocol')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], 42)
        self.assertIs(result['exit_value'], False)
        self.assertManagers(result, (True, True, None))


    def test_async_protocol_methods(self):
        result = self.run_shape('direct_async_protocol')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], 42)
        self.assertIs(result['exit_value'], False)
        self.assertManagers(result, (True, True, None))


class TestSyncContextManager(ContextManagerShapeTestCase):
    """
    with over a koskari-defined manager whose __enter__ and __exit__ are
    decorated with @demand
    """

    def test_no_binding(self):
        """
        with runs enter and exit around the body and yields the body value
        """

        result = self.run_shape('sync_no_binding')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "body")
        self.assertManagers(result, (True, True, None))


    def test_binding_gets_enter_value(self):
        """
        The binding form binds whatever __enter__ returned
        """

        result = self.run_shape('sync_binding')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], 42)
        self.assertManagers(result, (True, True, None))


    def test_exception_propagates(self):
        """
        A body exception reaches __exit__ and then propagates, since
        __exit__ returns false
        """

        result = self.run_shape('sync_raises')
        self.assertEqual(result['outcome'], 'raised')
        self.assertEqual(result['exc_type'], 'ValueError')
        self.assertEqual(result['exc_str'], "oops")
        self.assertManagers(result, (True, True, 'ValueError'))


    def test_exception_swallowed(self):
        """
        __exit__ returning true swallows the body exception and execution
        continues after the with block
        """

        result = self.run_shape('sync_swallowed')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "after")
        self.assertManagers(result, (True, True, 'ValueError'))


    def test_nested(self):
        """
        Nested with blocks enter outer-first and both bindings are live in
        the inner body
        """

        result = self.run_shape('sync_nested')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], [42, 42])
        self.assertManagers(result, (True, True, None), (True, True, None))


class TestAsyncContextManager(ContextManagerShapeTestCase):
    """
    with over a koskari-defined manager implementing the async protocol.
    __aenter__ and __aexit__ carry no decorator: the evaluator awaits them.
    """

    def test_no_binding(self):
        """
        with awaits __aenter__ and __aexit__ around the body and yields the
        body value
        """

        result = self.run_shape('async_no_binding')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "body")
        self.assertManagers(result, (True, True, None))


    def test_binding_gets_enter_value(self):
        """
        The binding form binds the awaited __aenter__ value
        """

        result = self.run_shape('async_binding')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], 42)
        self.assertManagers(result, (True, True, None))


    def test_exception_propagates(self):
        """
        A body exception reaches __aexit__ and then propagates, since
        __aexit__ returns false
        """

        result = self.run_shape('async_raises')
        self.assertEqual(result['outcome'], 'raised')
        self.assertEqual(result['exc_type'], 'ValueError')
        self.assertEqual(result['exc_str'], "oops")
        self.assertManagers(result, (True, True, 'ValueError'))


    def test_exception_swallowed(self):
        """
        __aexit__ returning true swallows the body exception and execution
        continues after the with block
        """

        result = self.run_shape('async_swallowed')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "after")
        self.assertManagers(result, (True, True, 'ValueError'))


    def test_nested(self):
        """
        Nested with blocks over async managers enter outer-first and both
        bindings are live in the inner body
        """

        result = self.run_shape('async_nested')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], [42, 42])
        self.assertManagers(result, (True, True, None), (True, True, None))


class TestDynWithContextManager(ContextManagerShapeTestCase):
    """
    The same koskari-defined managers driven through dyn-with, the
    multi-shot extent
    """

    def test_no_binding(self):
        """
        dyn-with runs enter and exit around the body and yields the body
        value
        """

        result = self.run_shape('dyn_with_no_binding')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "body")
        self.assertManagers(result, (True, True, None))


    def test_binding_gets_enter_value(self):
        """
        The binding form binds whatever __enter__ returned
        """

        result = self.run_shape('dyn_with_binding')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], 42)
        self.assertManagers(result, (True, True, None))


    def test_exception_propagates(self):
        """
        A body exception reaches __exit__ and then propagates
        """

        result = self.run_shape('dyn_with_raises')
        self.assertEqual(result['outcome'], 'raised')
        self.assertEqual(result['exc_type'], 'ValueError')
        self.assertEqual(result['exc_str'], "oops")
        self.assertManagers(result, (True, True, 'ValueError'))


    def test_exception_swallowed(self):
        """
        __exit__ returning true swallows the body exception and execution
        continues after the extent
        """

        result = self.run_shape('dyn_with_swallowed')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "after")
        self.assertManagers(result, (True, True, 'ValueError'))


    def test_nested(self):
        """
        Nested dyn-with extents enter outer-first and both bindings are
        live in the inner body
        """

        result = self.run_shape('dyn_with_nested')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], [42, 42])
        self.assertManagers(result, (True, True, None), (True, True, None))


class TestDynWind(ContextManagerShapeTestCase):
    """
    dyn-wind whose before, body, and after segments each call a
    koskari-defined function
    """

    def test_segments_run_in_order(self):
        """
        before, body, then after; the extent yields the body value
        """

        result = self.run_shape('dyn_wind_normal')
        self.assertEqual(result['outcome'], 'value')
        self.assertEqual(result['value'], "value")
        self.assertEqual(result['record'], ["before", "body", "after"])


    def test_after_runs_when_body_raises(self):
        """
        A raising body still runs the after segment, then propagates
        """

        result = self.run_shape('dyn_wind_raises')
        self.assertEqual(result['outcome'], 'raised')
        self.assertEqual(result['exc_type'], 'ValueError')
        self.assertEqual(result['exc_str'], "oops")
        self.assertEqual(result['record'], ["before", "body", "after"])


# The end.
