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
test_exception_winding_repro - full-stack exception/winding/continuation
interactions from a standalone self-checking script.

The scenarios live in scripts/exception_winding.ki, which is pure Koskari
so it can also be run directly under the launcher (e.g. wrapped in
valgrind) without pytest.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-fable-5 via Claude Code
"""


import io
import os
import sys
import unittest

from koskari.cli.host import run_file_async
from koskari.module import new_module

import pytest


pytestmark = pytest.mark.phase_reproducers


_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')


class TestExceptionWindingReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Try/except, dyn-wind, and reset/shift interactions end to end.
    """

    async def test_exception_winding_script(self):
        mod = new_module('__main__')
        script = os.path.join(_SCRIPTS, 'exception_winding.ki')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, script)
        finally:
            sys.stdout = orig
        self.assertIn('exception-winding: OK', captured.getvalue())


# The end.
