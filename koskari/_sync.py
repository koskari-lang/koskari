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
koskari._sync

Async-to-sync primitives for running awaitables and async iterables
from synchronous contexts. Safe when called from sync code regardless
of whether an event loop is already running in the current thread.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
import threading
from concurrent.futures import Future, ThreadPoolExecutor


__all__ = ('run_sync', 'iter_sync')


_executor = None
_executor_lock = threading.Lock()


def _get_executor_loop():
    """
    Return the event loop for the dedicated executor thread.
    Creates the thread and loop lazily on first use.
    """

    global _executor

    with _executor_lock:
        if _executor is not None:
            return _executor

        loop_holder = [None]
        ready = threading.Event()

        def run_loop():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            loop_holder[0] = loop
            ready.set()
            loop.run_forever()

        thread = threading.Thread(target=run_loop, daemon=True)
        thread.start()
        ready.wait()
        _executor = loop_holder[0]
        return _executor


def _active_loop():
    try:
        asyncio.get_running_loop()
    except RuntimeError:
        return False
    else:
        return True


def run_sync(awaitable):
    """
    Run an awaitable to completion from synchronous code.

    If no event loop is running in the current thread, uses asyncio.run().
    If a loop is already running (e.g. in Jupyter or an async app), runs
    the awaitable in a dedicated executor thread to avoid deadlock.
    """

    if _active_loop():
        loop = _get_executor_loop()
        future = asyncio.run_coroutine_threadsafe(awaitable, loop)
        return future.result()
    else:
        return asyncio.run(awaitable)


class _SyncIterator:
    """
    Synchronous iterator that drives an async iterator.
    Reuses a single event loop for the entire iteration session.
    """

    def __init__(self, async_iterable):
        self._async_iterable = async_iterable
        self._aiter = None
        self._loop = None


    def _ensure_started(self):
        if self._aiter is not None:
            return
        self._aiter = self._async_iterable.__aiter__()

        if _active_loop():
            self._loop = _get_executor_loop()
        else:
            self._loop = asyncio.new_event_loop()


    def __iter__(self):
        self._ensure_started()
        return self


    def __next__(self):
        self._ensure_started()

        try:
            awaitable = self._aiter.__anext__()

            if _active_loop():
                future = asyncio.run_coroutine_threadsafe(awaitable, self._loop)
                result = future.result()
            else:
                result = self._loop.run_until_complete(awaitable)
            return result

        except StopIteration as e:
            return e.value

        except StopAsyncIteration:
            raise StopIteration from None


def iter_sync(async_iterable):
    """
    Return a synchronous iterator that drives the given async iterable.

    Reuses a single event loop for the entire iteration session when
    called from a thread that already has a running loop.
    """

    return _SyncIterator(async_iterable)


# The end.
