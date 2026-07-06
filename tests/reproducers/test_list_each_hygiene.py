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
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_list_each_hygiene

Reproducer: list-each leaked its accumulator into a colliding loop var.

list-each expands with an accumulator the macro names ``col``. When the
user's loop variable is also ``col``, references to ``col`` nested inside
the body (a call, an f-string) resolved to the gensym-renamed accumulator
instead of the loop value. The finalize hygiene pass over-processed the
substituted body: after peeling the single Datum wrapper on a BODY region
capture, its interior symbols were bare and got renamed when they matched a
template-introduced binder name. The fix protects substituted subtrees from
renaming during finalize.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
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


def _script(name):
    return os.path.join(_SCRIPTS, name)


class TestListEachHygiene(unittest.IsolatedAsyncioTestCase):
    """
    list-each with a loop variable named ``col`` must bind the loop value,
    not the macro's accumulator.
    """

    async def _run_script(self, name):
        mod = new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, _script(name))
        finally:
            sys.stdout = orig
        return captured.getvalue()

    async def test_colliding_loop_var_binds_loop_value(self):
        out = await self._run_script('list_each_hygiene.ki')
        # nested call body: (+ col 10) over [1 2 3]
        self.assertIn('[11, 12, 13]', out)
        # f-string body: f"<{col}>" over ["a" "b" "c"]
        self.assertIn("['<a>', '<b>', '<c>']", out)


# The end.
