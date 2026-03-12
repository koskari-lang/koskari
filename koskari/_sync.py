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


__all__ = ('as_coroutine', 'iter_sync', 'run_sync')


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
    """
    Return True if an event loop is already running in the current thread.
    """

    try:
        asyncio.get_running_loop()
    except RuntimeError:
        return False
    else:
        return True


def _eager_try_complete(awaitable):
    """
    Try one send(None) on the awaitable's iterator.

    Returns (iterator, yielded_value) if suspended.  Raises StopIteration
    with value if it completes immediately.
    """

    iterator = awaitable.__await__()
    yielded = iterator.send(None)
    return (iterator, yielded)


async def _drive_await_iterator(iterator, first_yielded):
    """
    Drive a suspended await iterator from its first yielded value to completion.
    Handles None yield (e.g. from asyncio.sleep) as reschedule.
    """

    value = first_yielded
    while True:
        if value is None:
            result = await asyncio.sleep(0)
        else:
            result = await value
        try:
            value = iterator.send(result)
        except StopIteration as e:
            return e.value if e.value is not None else None


async def as_coroutine(awaitable):
    """
    Return a coroutine that, when awaited, awaits the given awaitable
    and returns its result. Accepts arbitrary awaitables (e.g. custom
    __await__), not just coroutines. asyncio.run requires a coroutine,
    so use this to adapt before passing to run_sync or asyncio.run.
    """

    return await awaitable


def run_sync(awaitable):
    """
    Run an awaitable to completion from synchronous code.

    Accepts arbitrary awaitables; wraps non-coroutines via as_coroutine.
    Tries an eager step (one send(None)) first; if the awaitable completes
    immediately, returns without involving the event loop.  If it suspends,
    drives it via asyncio.run or the executor.  If a loop is already running
    in the current thread, uses the executor to avoid deadlock (e.g. run_sync
    from within an async task or nested run_sync).
    """

    if not asyncio.iscoroutine(awaitable):
        awaitable = as_coroutine(awaitable)

    try:
        iterator, first_yielded = _eager_try_complete(awaitable)
    except StopIteration as e:
        return e.value if e.value is not None else None

    awaitable = _drive_await_iterator(iterator, first_yielded)
    if _active_loop():
        loop = _get_executor_loop()
        future = asyncio.run_coroutine_threadsafe(awaitable, loop)
        return future.result()
    return asyncio.run(awaitable)


class SyncIterator:
    """
    Synchronous iterator that drives an async iterator.
    Reuses a single event loop for the entire iteration session.
    """

    def __init__(self, async_iterable):
        self._async_iterable = async_iterable
        self._aiter = None
        self._loop = None
        self._owns_loop = False


    def _ensure_started(self):
        if self._aiter is not None:
            return

        self._aiter = self._async_iterable.__aiter__()

        if _active_loop():
            self._loop = _get_executor_loop()
        else:
            self._loop = asyncio.new_event_loop()
            self._owns_loop = True


    def _close_owned_loop(self):
        if not self._owns_loop or self._loop is None:
            return

        self._loop.close()
        self._loop = None
        self._owns_loop = False


    def __iter__(self):
        self._ensure_started()
        return self


    def __next__(self):
        self._ensure_started()
        running_in_loop = _active_loop()

        try:
            awaitable = self._aiter.__anext__()

            if running_in_loop:
                future = asyncio.run_coroutine_threadsafe(awaitable, self._loop)
                result = future.result()
            else:
                result = self._loop.run_until_complete(awaitable)
            return result

        except (StopIteration, StopAsyncIteration):
            self._close_owned_loop()
            raise StopIteration from None

        except Exception:
            self._close_owned_loop()
            raise


    def __del__(self):
        self._close_owned_loop()


def iter_sync(async_iterable):
    """
    Return a synchronous iterator that drives the given async iterable.

    Reuses a single event loop for the entire iteration session when
    called from a thread that already has a running loop.
    """

    return SyncIterator(async_iterable)


# The end.
