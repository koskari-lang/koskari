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
:ai-assistant: Auto via Cursor
"""


import struct


KIO_MAGIC = b'KIO\x00\x00\x00\x00\x00'

FORMAT_VERSION = 2

FINALIZE_PIPELINE_VERSION = 1

STRUCT_LAYOUT_VERSIONS = 0x01

FLAG_SOFTENED = 0x01
FLAG_HAS_DEBUG = 0x02
FLAG_COMPRESSED = 0x04

# Bump when KosOpCode enum values or softened CODEP instruction semantics
# change. Encoded as four little-endian u32 in the .kio header (16 bytes);
# no build-time or runtime dependency on opcodes.incl in the installed
# package.
BYTECODE_ABI_MAJOR = 0
BYTECODE_ABI_MINOR = 1
BYTECODE_ABI_PATCH = 0


def bytecode_abi_ident_bytes():
    """
    Return the 16-byte bytecode ABI identifier for the fixed .kio header.

    The wire format is four little-endian u32 values: major, minor, patch,
    and a reserved word (zero). Increment these constants when opcode
    numbering or instruction-stream encoding changes.

    :return: bytes of length 16
    """

    return struct.pack(
        '<IIII',
        BYTECODE_ABI_MAJOR,
        BYTECODE_ABI_MINOR,
        BYTECODE_ABI_PATCH,
        0)


# The end.
