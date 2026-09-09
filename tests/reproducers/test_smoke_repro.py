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
test_smoke_repro - minimal full-stack reproducer for the reproducers phase

These scenarios exercise multiple layers together (reader, compile, importer,
runtime). They run in the final pytest phase after unit and integration tests.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import io
import os
import sys
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')


def _script(name):
    return os.path.join(_SCRIPTS, name)


class TestSmokeReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Sanity check that the reproducers phase and script layout work end-to-end.
    """

    async def test_smoke_script_prints_computed_value(self):
        mod = new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, _script('smoke.ki'))
        finally:
            sys.stdout = orig
        self.assertIn('7', captured.getvalue())


# The end.
