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
tests.reproducers.test_async_exception_delivery

An asynchronous exception set with PyThreadState_SetAsyncExc, and a
callback added with Py_AddPendingCall, are serviced at a cooperation
point of a Koskari stretch that never returns to Python's own loop.
The loops below call only C functions, so no Python frame runs inside
them to service either on the evaluator's behalf.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import ctypes
import threading
import time
import unittest

import pytest

from koskari.lib import demand_call
from koskari.module import load_module_source_async, new_module
from tests import needs_threads


pytestmark = pytest.mark.phase_reproducers


SRC = """
import time

def spin [state]:
    var n 0
    while (not (state.get "stop")):
        set! n (+ n 1)
        (state.__setitem__ "n" n)
    "stopped"

def spin-catching [state]:
    try:
        (spin state)
    except [e Interrupt]:
        (state.__setitem__ "caught" e)
        "caught"

def spin-adding [state add callback deadline]:
    var n 0
    (add callback None)
    while (and (not (state.get "flag")) (< (time.monotonic) deadline)):
        set! n (+ n 1)
    (state.__setitem__ "seen" (state.get "flag"))
    n
"""


class Interrupt(Exception):
    pass


PENDING_CALL = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)


def set_async_exc(thread, exc_type):
    """
    Pend exc_type on thread with PyThreadState_SetAsyncExc

    :return: the number of thread states modified
    """

    setter = ctypes.pythonapi.PyThreadState_SetAsyncExc
    setter.argtypes = (ctypes.c_ulong, ctypes.py_object)
    setter.restype = ctypes.c_int
    return setter(ctypes.c_ulong(thread.ident), ctypes.py_object(exc_type))


def wait_for_progress(state, bound=5.0):
    """
    Wait until the loop's counter is moving, or the bound runs out
    """

    end = time.monotonic() + bound
    while time.monotonic() < end:
        if state.get("n", 0) > 0:
            return True
        time.sleep(0.001)
    return False


@needs_threads
class TestAsyncExceptionDelivery(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        module = new_module("async_exc_delivery")
        vars(module)["__file__"] = "<async_exc_delivery>"
        vars(module)["Interrupt"] = Interrupt
        await load_module_source_async(module, SRC)
        self.ns = vars(module)


    def run_worker(self, fn, exc_type):
        """
        Run fn on a worker thread, pend exc_type on it once its loop is
        moving, and join it

        :return: the worker's state, its result list, and the
            excepthook's records
        """

        state = {}
        results = []
        hooked = []

        def worker():
            results.append(demand_call(fn, state))

        prior = threading.excepthook
        threading.excepthook = lambda args: hooked.append(args.exc_type)
        try:
            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            try:
                self.assertTrue(wait_for_progress(state),
                                "the worker's loop never started")
                self.assertEqual(set_async_exc(thread, exc_type), 1)
                thread.join(5)
                alive = thread.is_alive()
            finally:
                state["stop"] = True
                thread.join(5)
        finally:
            threading.excepthook = prior

        self.assertFalse(alive, "the asynchronous exception was not raised"
                         " inside the Koskari loop")
        return state, results, hooked


    async def test_async_exception_ends_the_loop(self):
        """
        SystemExit set on a thread looping in Koskari ends the thread
        """

        _state, results, hooked = self.run_worker(
            self.ns["spin"], SystemExit)

        self.assertEqual(results, [])
        self.assertEqual(hooked, [SystemExit])


    async def test_async_exception_caught_by_try(self):
        """
        An asynchronous exception is an ordinary raise a Koskari try
        catches
        """

        state, results, hooked = self.run_worker(
            self.ns["spin-catching"], Interrupt)

        self.assertEqual(results, ["caught"])
        self.assertIsInstance(state["caught"], Interrupt)
        self.assertEqual(hooked, [])


    async def test_pending_call_runs_in_the_loop(self):
        """
        A callback added with Py_AddPendingCall runs inside a Koskari
        loop on the main thread
        """

        # the loop adds the call itself, through a C function: a Python
        # thread adding it would need the GIL, and the drop that hands
        # the GIL over is a poll that runs the call anyway
        state = {}

        def ran(_arg):
            state["flag"] = True
            return 0

        callback = PENDING_CALL(ran)
        add = ctypes.pythonapi.Py_AddPendingCall
        add.argtypes = (PENDING_CALL, ctypes.c_void_p)
        add.restype = ctypes.c_int

        started = time.monotonic()
        demand_call(self.ns["spin-adding"], state, add, callback,
                    started + 5.0)
        elapsed = time.monotonic() - started

        self.assertTrue(state.get("seen"),
                        "the pending call never ran inside the loop")
        self.assertLess(elapsed, 4.0)


# The end.
