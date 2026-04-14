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

Implementation is delegated to the top-level ``koskari_importer`` module so
``import koskari`` can register the hook without loading this subpackage.

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
:ai-assistant: Auto via Cursor
"""


from koskari_importer import install, is_installed, uninstall


__all__ = (
    'install',
    'is_installed',
    'uninstall',
)


# The end.
