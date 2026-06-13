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
koskari.extras.setuptools

Build-time helpers for koskari_scripts packaging. Application packages
declare koskari_scripts entry points and list koskari in build-system.requires
so this finalize hook mirrors installed commands into console_scripts.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from .finalize import mirror_koskari_scripts
from .validate import validate_koskari_scripts, warn_koskari_scripts_metadata

__all__ = (
    'mirror_koskari_scripts',
    'validate_koskari_scripts',
    'warn_koskari_scripts_metadata',
)


# The end.
