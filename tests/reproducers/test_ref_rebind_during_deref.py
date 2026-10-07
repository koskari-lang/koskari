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
tests.reproducers.test_ref_rebind_during_deref

A variable read through a ref that runs user code, where that code
rebinds the variable, completes against the ref it started with and
the next read sees the new binding.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'ref_rebind_during_deref.ki')


class TestRefRebindDuringDeref(unittest.IsolatedAsyncioTestCase):

    async def test_variable_read_while_its_ref_is_rebound(self):
        mod = new_module('ref_rebind_during_deref')
        await run_file_async(mod, _SCRIPT)
        ns = vars(mod)
        self.assertEqual('keyerror', ns['first'])
        self.assertEqual(42, ns['second'])


# The end.
