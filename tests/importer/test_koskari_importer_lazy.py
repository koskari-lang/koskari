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
Ensure import koskari does not eagerly load the compiler or native lib.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


import subprocess
import sys

import pytest

pytestmark = pytest.mark.phase_importer


def test_import_koskari_subprocess_does_not_load_compile_or_lib():
    """
    Fresh interpreter: ``import koskari`` must not load compile or lib yet.
    """

    code = (
        'import sys\n'
        'import koskari\n'
        'assert "koskari.builtins" not in sys.modules\n'
        'assert "koskari.bootstrap" not in sys.modules\n'
        'assert "koskari.compile" not in sys.modules\n'
        'assert "koskari.lib" not in sys.modules\n'
        'assert "koskari.importer" in sys.modules\n'
    )
    subprocess.check_call([sys.executable, '-c', code])


# The end.
