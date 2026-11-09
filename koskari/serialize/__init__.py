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
koskari.serialize

Spec-driven .kio read/write and incremental adapters.
Use ``compile_ki_file_to_kio(..., source_filename=...)`` when the path stored
for runtime tracebacks should differ from the build-time ``ki_path`` (same
source bytes required).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from .codex import KioFile, KioHeader, KioMeta, dump_kio, load_kio
from .incremental import (
    compile_ki_file_to_kio, compile_ki_file_to_kio_async,
    compile_ki_source_to_kio, compile_top_level_to_codes_async)


__all__ = (
    'compile_ki_file_to_kio',
    'compile_ki_file_to_kio_async',
    'compile_ki_source_to_kio',
    'compile_top_level_to_codes_async',
    'KioFile',
    'KioHeader',
    'KioMeta',
    'dump_kio',
    'load_kio',
)


# The end.
