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
koskari.importer

Install/remove hooks that allow .ki files to be imported transparently.

The primary entry points are install() and is_installed().  Installing
inserts KoskariPathFinder at the front of sys.meta_path so that .ki
source files are found before Python's own PathFinder can claim a
directory as a namespace package.

Auto-installation is suppressed when the environment variable
KOSKARI_NOIMPORTER is set to a non-zero value, or when the -X
KOSKARI_NOIMPORTER Python flag is set.  koskari/__init__.py calls
_auto_enable_importer() at import time.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import sys


__all__ = (
    'install',
    'is_installed',
)


def is_installed():
    """
    Return True if KoskariPathFinder is currently in sys.meta_path.

    KoskariPathFinder is inserted as a class (like Python's own PathFinder),
    so we check for the class object itself, not instances of it.

    :return: bool
    """

    from .importlib import KoskariPathFinder
    return KoskariPathFinder in sys.meta_path


def install():
    """
    Insert KoskariPathFinder at the front of sys.meta_path.

    KoskariPathFinder is inserted as a class object, following the same
    convention as Python's own PathFinder.  Calling install() when the
    finder is already present is a no-op.
    """

    if is_installed():
        return

    from .importlib import KoskariPathFinder
    sys.meta_path.insert(0, KoskariPathFinder)


def uninstall():
    """
    Remove KoskariPathFinder from sys.meta_path.

    Calling uninstall() when the finder is not present is a no-op.
    """

    from .importlib import KoskariPathFinder
    sys.meta_path[:] = [
        f for f in sys.meta_path if f is not KoskariPathFinder
    ]


# The end.
