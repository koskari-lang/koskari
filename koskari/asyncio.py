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
import warnings
from functools import wraps
from json import loads as _json_loads
from subprocess import CalledProcessError

from koskari.bootstrap import add_builtin, add_builtin_type, builtin


__all__ = (
    "ProcessResult",
    "as_coroutine",
    "coroutine_fn",
    "create_task",
    "run_process",
    "run_until_complete",
)


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
    Create and schedule a Task from an awaitable, returning the Task
    itself. Tasks are handles: they are excluded from implicit auto-await,
    support cancellation and done-callbacks, and resolve under explicit
    await.

    Python coroutines, Koskari Coroutines, and defers all pass
    asyncio.iscoroutine and schedule directly; exotic non-coroutine
    awaitables are adapted through as_coroutine first. Note that passing
    a defer consumes it -- the Task owns its single shot.
    """

    if not asyncio.iscoroutine(coro):
        coro = as_coroutine(coro)
    return asyncio.create_task(coro)


def run_until_complete(loop, coro):
    """
    Run a coroutine until completion. Will automatically convert a defer or
    Koskari Coroutine into a Python coroutine object. Blocks until the
    coroutine completes and returns the result or raises an exception.

    :param loop: The event loop to use. If None, the default event loop
       will be used.
    :param coro: The coroutine to run.
    :returns: The result of the coroutine.
    """

    if not asyncio.iscoroutine(coro):
        coro = as_coroutine(coro)

    if loop is None:
        loop = _default_loop()

    return loop.run_until_complete(coro)


def _default_loop():
    """
    The current event loop, or a new one installed as current when none
    is set. Python 3.12 and 3.13 would create one implicitly with a
    DeprecationWarning and 3.14 refuses outright; the warning is raised
    here so every version takes the same path.
    """

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", DeprecationWarning)
            return asyncio.get_event_loop()
    except (RuntimeError, DeprecationWarning):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        return loop


add_builtin("sleep", asyncio.sleep)


class ProcessResult:
    """
    The captured outcome of a completed subprocess.

    Bundles the argv, exit status, and raw stdout/stderr of a finished
    process in the manner of an HTTP client's Response object: capture
    everything, interpret afterwards. Helpers decode and parse the
    captured stdout; raise_for_status opts into strict exit-status
    handling.
    """

    def __init__(self, args, returncode, stdout, stderr):
        self.args = tuple(args)
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


    def __repr__(self):
        return (f"<ProcessResult {self.args[0]!r}"
                f" returncode={self.returncode}>")


    @property
    def text(self):
        """
        The captured stdout, decoded as utf-8
        """

        return self.stdout.decode()


    @property
    def stderr_text(self):
        """
        The captured stderr, decoded as utf-8
        """

        return self.stderr.decode()


    def json(self):
        """
        The captured stdout parsed as a single JSON document
        """

        return _json_loads(self.text)


    def jsonl(self):
        """
        The captured stdout parsed as JSON Lines: a list with one parsed
        document per non-blank line
        """

        return [_json_loads(line)
                for line in self.text.splitlines()
                if line.strip()]


    def yaml(self):
        """
        The captured stdout parsed as a single YAML document. Requires
        the optional PyYAML package
        """

        try:
            from yaml import safe_load
        except ImportError:
            raise ImportError(
                "ProcessResult.yaml requires the PyYAML package") from None

        return safe_load(self.text)


    def raise_for_status(self):
        """
        Raise CalledProcessError when the process exited nonzero.

        Returns self otherwise, so strict callers can chain, e.g.
        ``(.json (.raise_for_status result))``
        """

        if self.returncode:
            raise CalledProcessError(
                self.returncode, self.args,
                output=self.stdout, stderr=self.stderr)
        return self


add_builtin_type("ProcessResult", ProcessResult)


@builtin("run-process")
async def run_process(*args):
    """
    Run an argv to completion in a subprocess, returning a ProcessResult

    Launches the argv with asyncio, captures stdout and stderr, and
    waits for the process to exit. The exit status is reported on the
    result, never raised; use ProcessResult.raise_for_status for strict
    handling.
    """

    proc = await asyncio.create_subprocess_exec(
        *args,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE)
    stdout, stderr = await proc.communicate()

    return ProcessResult(args, proc.returncode, stdout, stderr)


# The end.
