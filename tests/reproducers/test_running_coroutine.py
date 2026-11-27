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
tests.reproducers.test_running_coroutine

Full-stack reproducer: a Koskari coroutine reached from inside its own
run -- awaited by itself or by a callee, sent, thrown into or closed
from a Python helper it called, or from inside the Python awaitable it
is forwarding into -- is refused with ValueError, as CPython refuses a
running coroutine. Its __await__ succeeds, as CPython's does; the step
that follows is refused. A coroutine suspended under another awaiter
is still refused with RuntimeError. Each expected outcome is taken
from the equivalent Python coroutine on the running version

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import os
import types
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'running_coroutine.ki')


class Suspender:
    """
    A Python awaitable that suspends once with "suspended" and
    completes with "woken"
    """

    def __await__(self):
        yield "suspended"
        return "woken"


class Forwarded:
    """
    A Python awaitable whose second step, run while the waiting
    coroutine forwards into it, applies action to that coroutine and
    records the outcome
    """

    def __init__(self, box, action):
        self.box = box
        self.action = action


    def __await__(self):
        yield "suspended"
        self.box.outcome = attempt(self.action, self.box.c)
        return "woken"


def attempt(action, c):
    """
    Apply action to c

    :return: "ok", or (exception type, message)
    """

    try:
        action(c)
    except Exception as exc:
        return (type(exc), str(exc))
    return "ok"


def run_out(c):
    """
    Step c from Python to its end

    :return: its value
    """

    try:
        while True:
            assert c.send(None) == "suspended"
    except StopIteration as done:
        return done.value


def refusal(result):
    """
    The refusal a catching awaiter came back with

    :param result: ("raised", exception), or a value
    :return: (exception type, message), or None for a value
    """

    if isinstance(result, tuple) and result and result[0] == "raised":
        return (type(result[1]), str(result[1]))
    return None


async def py_catch_self(box):
    try:
        return await box.c
    except BaseException as e:
        return ("raised", e)


async def py_callee(box):
    try:
        return await box.c
    except BaseException as e:
        return ("raised", e)


async def py_caller(box):
    return await py_callee(box)


async def py_call_helper(box, helper):
    helper(box.c)
    return "done"


async def py_await_object(aw):
    return await aw


async def py_catch_await(aw):
    try:
        return await aw
    except BaseException as e:
        return ("raised", e)


async def load_module():
    mod = new_module('tests.reproducers.running_coroutine')
    init_module(mod, os.path.abspath(_SCRIPT))
    await load_module_async(mod)
    return mod


def python_await(c):
    py_await_object(c).send(None)


def send_none(c):
    c.send(None)


def throw_key_error(c):
    c.throw(KeyError("k"))


def close(c):
    c.close()


def await_method(c):
    c.__await__()


class TestRunningCoroutine(unittest.TestCase):
    """
    A running Koskari coroutine refuses a re-entry as CPython refuses a
    running coroutine
    """

    def setUp(self):
        self.mod = asyncio.run(load_module())


    def self_await(self, catch_self):
        box = types.SimpleNamespace()
        box.c = catch_self(box)
        return refusal(run_out(box.c))


    def test_self_await_is_refused(self):
        found = self.self_await(self.mod.catch_self)
        expect = self.self_await(py_catch_self)
        self.assertIs(expect[0], ValueError)
        self.assertEqual(found, expect)


    def caller_awaited(self, caller):
        box = types.SimpleNamespace()
        box.c = caller(box)
        return refusal(run_out(box.c))


    def test_callee_awaiting_its_caller_is_refused(self):
        found = self.caller_awaited(self.mod.caller)
        expect = self.caller_awaited(py_caller)
        self.assertIs(expect[0], ValueError)
        self.assertEqual(found, expect)


    def from_helper(self, call_helper, action):
        """
        Run a coroutine whose body calls a helper applying action to
        that same coroutine

        :return: (the action's outcome, the coroutine's value)
        """

        box = types.SimpleNamespace()
        outcome = []
        box.c = call_helper(box, lambda c: outcome.append(attempt(action, c)))
        value = run_out(box.c)
        return (outcome, value)


    def compare_helper(self, action):
        found = self.from_helper(self.mod.call_helper, action)
        expect = self.from_helper(py_call_helper, action)
        self.assertEqual(found, expect)
        return found


    def test_send_from_inside_is_refused(self):
        found = self.compare_helper(send_none)
        self.assertIs(found[0][0][0], ValueError)


    def test_throw_from_inside_is_refused(self):
        found = self.compare_helper(throw_key_error)
        self.assertIs(found[0][0][0], ValueError)


    def test_close_from_inside_is_refused(self):
        found = self.compare_helper(close)
        self.assertIs(found[0][0][0], ValueError)


    def test_await_method_from_inside_succeeds(self):
        self.assertEqual(self.compare_helper(await_method), (["ok"], "done"))


    def test_demand_from_inside_is_refused(self):
        """
        demand has no CPython counterpart; it steps the coroutine as
        send does
        """

        found = self.from_helper(self.mod.call_helper, lambda c: c.demand())
        self.assertEqual(
            found,
            ([(ValueError, "coroutine already executing")], "done"))


    def during_forward(self, await_object, action):
        """
        Run a coroutine waiting on a Forwarded awaitable which applies
        action to that same coroutine during the forward

        :return: (the action's outcome, the coroutine's value)
        """

        box = types.SimpleNamespace()
        box.c = await_object(Forwarded(box, action))
        value = run_out(box.c)
        return (box.outcome, value)


    def compare_forward(self, action):
        found = self.during_forward(self.mod.await_object, action)
        expect = self.during_forward(py_await_object, action)
        self.assertEqual(found, expect)
        return found


    def test_await_method_during_forward_succeeds(self):
        self.assertEqual(
            self.compare_forward(await_method), ("ok", "woken"))


    def test_python_await_during_forward_is_refused(self):
        found = self.compare_forward(python_await)
        self.assertIs(found[0][0], ValueError)


    def test_send_during_forward_is_refused(self):
        found = self.compare_forward(send_none)
        self.assertIs(found[0][0], ValueError)


    def test_throw_during_forward_is_refused(self):
        found = self.compare_forward(throw_key_error)
        self.assertIs(found[0][0], ValueError)


    def test_close_during_forward_is_refused(self):
        found = self.compare_forward(close)
        self.assertIs(found[0][0], ValueError)


    def second_awaiter(self, await_object, catch_await):
        """
        Suspend a coroutine under a first awaiter, then await it from a
        second

        :return: (the second awaiter's refusal, the first's value)
        """

        c = await_object(Suspender())
        first = py_await_object(c)
        self.assertEqual(first.send(None), "suspended")
        second = refusal(run_out(catch_await(c)))
        return (second, run_out(first))


    def test_suspended_coroutine_refuses_a_second_awaiter(self):
        found = self.second_awaiter(
            self.mod.await_object, self.mod.catch_await)
        expect = self.second_awaiter(py_await_object, py_catch_await)
        self.assertEqual(
            expect,
            ((RuntimeError, "coroutine is being awaited already"), "woken"))
        self.assertEqual(found, expect)


# The end.
