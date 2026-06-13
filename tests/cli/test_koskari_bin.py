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
tests.cli.test_koskari_bin

Smoke tests for the native koskari executable when built or installed.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import os
import shutil
import subprocess
import unittest
from pathlib import Path

import pytest

pytestmark = pytest.mark.phase_cli


def _koskari_binary():
    root = Path(__file__).resolve().parents[2]
    build_bin = root / 'build' / 'koskari-bin' / 'koskari'
    if build_bin.is_file():
        return str(build_bin)

    which = shutil.which('koskari')
    if which:
        return which

    return None


@pytest.mark.skipif(_koskari_binary() is None, reason='native koskari not built')
class TestKoskariBinary(unittest.TestCase):

    def test_version_flag(self):
        binary = _koskari_binary()
        env = dict(os.environ)
        env.pop('PYTHONSAFEPATH', None)
        proc = subprocess.run(
            [binary, '--version'],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )
        self.assertEqual(0, proc.returncode)
        self.assertIn('koskari', proc.stdout)


# The end.
