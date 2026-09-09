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
test_yield_from_koskari_gen - yield-from over a koskari generator delegate

A koskari generator used as the delegate of a yield-from must re-emit its
values to the outer consumer.  Regressed so that consuming the outer
generator via for-each saw none of the delegate's values.

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


class TestYieldFromKoskariGen(unittest.IsolatedAsyncioTestCase):
    """
    yield-from delegating to a koskari generator must forward its values.
    """

    async def test_yield_from_delegates_to_koskari_generator(self):
        mod = new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, _script('yield_from_koskari_gen.ki'))
        finally:
            sys.stdout = orig

        out = captured.getvalue()
        self.assertIn('SEEN 5', out)
        self.assertIn('SUM 10', out)


# The end.
