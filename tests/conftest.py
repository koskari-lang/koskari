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
Shared pytest configuration for the whole test tree.

Puts the repository root on ``sys.path`` so tests can import the
developer tooling under ``tools/``. The suite runs with ``-I`` and
``PYTHONSAFEPATH=1``, which keep the working directory off the path,
and pytest's importlib mode synthesizes the ``tests`` package without
adding the root either -- so without this, importing ``tools`` depends
on some earlier test module having inserted the root as a side effect.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

if sys.platform == 'emscripten':
    from tests.pyodide_harness import install

    install()

    try:
        import pyarrow  # noqa: F401
    except ImportError:
        collect_ignore = ["tools/test_corpus_export.py"]


# The end.
