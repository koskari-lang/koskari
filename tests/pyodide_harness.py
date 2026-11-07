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
tests.pyodide_harness

Lets the suite run under Pyodide. Pyodide keeps its browser event loop
marked as running for the life of the interpreter, which makes
asyncio.Runner, and so unittest.IsolatedAsyncioTestCase, refuse to start.
The tests get a plain event loop of their own instead

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import time


def spin_sleep(seconds):
    """
    Wait without yielding to node. Pyodide's time.sleep suspends the wasm
    stack on a promise, and node exits with status 0 and no test report
    when it does so under pytest

    :param seconds: seconds to wait
    """

    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        pass


class IdleSelector:
    """
    The selector an event loop waits on when there is no I/O to watch
    """

    def select(self, timeout=None):
        """
        Wait out the time until the loop's next scheduled callback. With
        none scheduled and no I/O to watch, nothing can ever wake the loop

        :param timeout: seconds to wait, or None
        :return: no events
        :raises RuntimeError: the loop has nothing left to run
        """

        if timeout is None:
            raise RuntimeError("event loop is waiting on nothing")
        if timeout:
            time.sleep(timeout)
        return []


    def close(self):
        """
        Nothing to release
        """


class HarnessLoop(asyncio.BaseEventLoop):
    """
    An event loop with timers and callbacks and no I/O
    """

    def __init__(self):
        super().__init__()
        self._selector = IdleSelector()


    def _process_events(self, event_list):
        """
        The idle selector reports no events
        """


    def _write_to_self(self):
        """
        Nothing blocks on a selector that another thread could wake
        """


def install():
    """
    Release the interpreter from Pyodide's running loop, have asyncio
    create harness loops, and make time.sleep safe under node
    """

    time.sleep = spin_sleep
    asyncio._set_running_loop(None)
    asyncio.new_event_loop = HarnessLoop
    asyncio.events.new_event_loop = HarnessLoop


# The end.
