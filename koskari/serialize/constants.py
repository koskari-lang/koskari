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
koskari.serialize.constants

Binary layout constants for the .kio format.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


KIO_MAGIC = b'KIO\x00\x00\x00\x00\x00'

# Semantic bytecode ABI for .kio (four little-endian u32 in the file header).
# Bump when KosOpCode values or softened CODEP instruction semantics change.
# MINOR bumped: MATCH_CASE's fourth EXTRA slot becomes the keep-subject
# flag (COUNT); a zero flag preserves the prior consume-on-match behavior.
BYTECODE_ABI_MAJOR = 0
BYTECODE_ABI_MINOR = 9
BYTECODE_ABI_MICRO = 0
BYTECODE_ABI_PATCH = 0


# The end.
