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
tests.reproducers.context_manager_runner

Child process for the koskari context-manager reproducers. Runs exactly one
named shape from scripts/koskari_context_manager.ki and prints a single
RESULT line of JSON describing what happened.

Each shape runs in its own process because a with block over a
koskari-defined manager can wedge the evaluator inside its C dispatch loop,
where no asyncio timeout and no signal-based interrupt can reach it. The
parent kills a wedged child; an in-process run would hang the suite.

usage: python tests/reproducers/context_manager_runner.py <shape>

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
import json
import os
import sys


RESULT_PREFIX = "RESULT "

_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
        'scripts', 'koskari_context_manager.ki')


# shape name -> (module function name, manager class names)
SHAPES = {
    'sync_no_binding': ('sync_no_binding', ('KosCM',)),
    'sync_binding': ('sync_binding', ('KosCM',)),
    'sync_raises': ('sync_raises', ('KosCM',)),
    'sync_swallowed': ('sync_swallowed', ('KosSwallowCM',)),
    'sync_nested': ('sync_nested', ('KosCM', 'KosCM')),
    'async_no_binding': ('async_no_binding', ('KosAsyncCM',)),
    'async_binding': ('async_binding', ('KosAsyncCM',)),
    'async_raises': ('async_raises', ('KosAsyncCM',)),
    'async_swallowed': ('async_swallowed', ('KosAsyncSwallowCM',)),
    'async_nested': ('async_nested', ('KosAsyncCM', 'KosAsyncCM')),
    'direct_protocol': (None, ()),
    'direct_async_protocol': (None, ()),
}


async def load():
    """
    Load the reproducer module on the async-hosted path
    """

    from koskari.module import init_module, load_module_async, new_module

    mod = new_module('tests.reproducers.koskari_context_manager')
    init_module(mod, _SCRIPT)
    await load_module_async(mod)
    return mod


def manager_state(mgr):
    """
    The protocol state a manager recorded, as JSON-safe values
    """

    exc = mgr.exc_type
    return {
        'entered': bool(mgr.entered),
        'exited': bool(mgr.exited),
        'exc_type': None if exc is None else getattr(exc, '__name__', str(exc)),
    }


async def run_direct(mod, is_async):
    """
    Call the protocol methods directly, bypassing with entirely
    """

    if is_async:
        mgr = mod.KosAsyncCM(42)
        entered = await mgr.__aenter__()
        exited = await mgr.__aexit__(None, None, None)
    else:
        mgr = mod.KosCM(42)
        entered = mgr.__enter__()
        exited = mgr.__exit__(None, None, None)

    return {
        'outcome': 'value',
        'value': entered,
        'exit_value': exited,
        'managers': [manager_state(mgr)],
    }


async def run_shape(mod, shape):
    """
    Run one with-block shape and report its outcome
    """

    if shape == 'direct_protocol':
        return await run_direct(mod, False)
    if shape == 'direct_async_protocol':
        return await run_direct(mod, True)

    fname, mgr_names = SHAPES[shape]
    mgrs = [getattr(mod, name)(42) for name in mgr_names]

    try:
        value = await getattr(mod, fname)(*mgrs)
    except Exception as exc:
        report = {'outcome': 'raised', 'exc_type': type(exc).__name__,
                  'exc_str': str(exc)}
    else:
        report = {'outcome': 'value', 'value': value}

    report['managers'] = [manager_state(mgr) for mgr in mgrs]
    return report


async def main(shape):
    mod = await load()
    return await run_shape(mod, shape)


if __name__ == '__main__':
    name = sys.argv[1]
    if name not in SHAPES:
        print(f"unknown shape: {name}", file=sys.stderr)
        sys.exit(2)

    print(RESULT_PREFIX + json.dumps(asyncio.run(main(name))), flush=True)


# The end.
