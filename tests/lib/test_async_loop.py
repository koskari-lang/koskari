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
tests.lib.test_async_loop

Smoke tests for koskari.lib.drive_async_loop.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio

import pytest

from koskari.lib import drive_async_loop

pytestmark = pytest.mark.phase_lib


async def _return_value(value):
    return value


def test_drive_async_loop_runs_coroutine():
    result = drive_async_loop(_return_value(42))
    assert result == 42


def test_drive_async_loop_is_native_callable():
    assert type(drive_async_loop).__name__ == 'builtin_function_or_method'


def test_drive_async_loop_delegates_to_same_module():
    from koskari.launcher import drive_async_loop as launcher_drive

    assert launcher_drive is drive_async_loop


# The end.
