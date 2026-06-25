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
test_asyncgen_gc_during_iteration - generator survives GC while iterated

A koskari generator iterated by a for-each must not be reaped by the cyclic
garbage collector mid-iteration.  The GEN node's tp_traverse reported its
borrowed owner to the collector, so under allocation pressure GC cleared the
still-live generator and iteration stopped after one item.  This is the bug
behind koslife's empty board after a large random fill.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Claude Code
"""


import io
import os
import sys
import unittest

from koskari.cli import run_file_async
from koskari.module import new_module

import pytest


pytestmark = pytest.mark.phase_reproducers


_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')


def _script(name):
    return os.path.join(_SCRIPTS, name)


class TestAsyncGenGCDuringIteration(unittest.IsolatedAsyncioTestCase):
    """
    A generator must yield every element even when GC runs mid-iteration.
    """

    async def test_generator_survives_gc_while_iterated(self):
        mod = new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, _script('asyncgen_gc_during_iteration.ki'))
        finally:
            sys.stdout = orig

        out = captured.getvalue()
        self.assertIn('SHOWN 800 EXPECTED 800', out)


# The end.
