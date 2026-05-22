"""
koskari.asyncio

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from functools import wraps
from koskari.lib import defer, Coroutine
from koskari.bootstrap import builtin, add_builtin
import asyncio


def as_coroutine(awaitable):
    async def coro():
        return await awaitable
    return coro()


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
    task = asyncio.create_task(coro)
    return defer(lambda: task)


@builtin("run-until-complete")
def run_until_complete(coro):
    if isinstance(coro, (defer, Coroutine)):
        coro = as_coroutine(coro)
    return asyncio.run_until_complete(coro)


add_builtin("sleep", asyncio.sleep)


# The end.
