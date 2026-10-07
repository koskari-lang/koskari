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
tests.reproducers.test_asyncgen_cleanup

A generator dropped while suspended is closed synchronously; what its
cleanup raises is reported through sys.unraisablehook instead of being
cleared.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import os
import sys
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'asyncgen_cleanup.ki')


class TestAsyncGenCleanup(unittest.TestCase):

    def test_cleanup_error_at_gc_is_reported(self):
        async def make():
            mod = new_module('asyncgen_cleanup')
            await run_file_async(mod, _SCRIPT)
            return vars(mod)['raising']

        raising = asyncio.run(make())
        seen = []
        prior = sys.unraisablehook
        sys.unraisablehook = lambda u: seen.append(repr(u.exc_value))
        try:
            it = iter(raising([]))
            next(it)
            del it
            gc.collect()
        finally:
            sys.unraisablehook = prior
        self.assertEqual(["ValueError('cleanup failed')"], seen)


# The end.
