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
koskari.importer.importlib

Re-exports the import machinery from the top-level ``koskari_importer``
module so existing ``from koskari.importer.importlib import ...`` continues
to work.  Implementation lives in ``koskari_importer`` to avoid pulling
``koskari.compile`` in when only the hook is needed.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


from koskari_importer import (
    KoskariFileFinder,
    KoskariPathFinder,
    KoskariSourceFileLoader,
    path_hooks,
    path_importer_cache,
)


__all__ = (
    'KoskariPathFinder',
    'KoskariSourceFileLoader',
    'KoskariFileFinder',
    'path_hooks',
    'path_importer_cache',
)


# The end.
