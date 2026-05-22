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

Python asyncio support for Koskari.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from functools import wraps
from koskari.lib import defer, Coroutine
from koskari.bootstrap import builtin, add_builtin
import asyncio


async def as_coroutine(awaitable):
    return await awaitable


@builtin("coroutine-fn")
def coroutine_fn(fn):
    @wraps(fn)
    async def wrapper(*args, **kwargs):
        return await fn(*args, **kwargs)
    return wrapper


@builtin("create-task")
def create_task(coro):
    if isinstance(coro, (defer, Coroutine)):
        coro = as_coroutine(coro)
    return defer(asyncio.create_task, coro)


@builtin("run-until-complete")
def run_until_complete(coro):
    if isinstance(coro, (defer, Coroutine)):
        coro = as_coroutine(coro)
    return asyncio.run_until_complete(coro)


add_builtin("sleep", asyncio.sleep)


# The end.
