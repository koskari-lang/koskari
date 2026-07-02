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
koskari.asyncio

Python asyncio support for Koskari. Primarily focuses on compatability with
asyncio loops which specifically check for the Python coroutine type.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
from functools import wraps
from types import CoroutineType as PythonCoroutine

from koskari.bootstrap import add_builtin, builtin
from koskari.lib import Coroutine, defer


async def as_coroutine(awaitable):
    """
    Convert an awaitable into a Python coroutine function. This is
    a compatibility layer for asyncio loops which specifically check
    for the coroutine type.
    """

    return await awaitable


@builtin("coroutine-fn")
def coroutine_fn(fn):
    """
    Convert an async function into a Python coroutine function. This is
    a compatibility layer for asyncio loops which specifically check
    for the coroutine type.
    """

    @wraps(fn)
    async def wrapper(*args, **kwargs):
        return await fn(*args, **kwargs)
    return wrapper


@builtin("create-task")
def create_task(coro):
    """
    Create and schedule a Task from an awaitable. Will automatically convert a
    defer or Koskari Coroutine into a Python coroutine object. The resulting
    Task will be wrapped in a defer object to prevent implicit auto-await.
    """

    if not isinstance(coro, PythonCoroutine):
        coro = as_coroutine(coro)
    return defer(asyncio.create_task, coro)


@builtin("run-until-complete")
def run_until_complete(coro):
    """
    Run a coroutine until completion. Will automatically convert a defer or
    Koskari Coroutine into a Python coroutine object. Blocks until the
    coroutine completes and returns the result or raises an exception.
    """

    if not isinstance(coro, PythonCoroutine):
        coro = as_coroutine(coro)
    return asyncio.run_until_complete(coro)


add_builtin("sleep", asyncio.sleep)


# The end.
