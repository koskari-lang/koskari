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


import hashlib
from pathlib import Path


KIO_MAGIC = b'KIO\x00\x00\x00\x00\x00'

FORMAT_VERSION = 2

FINALIZE_PIPELINE_VERSION = 1

STRUCT_LAYOUT_VERSIONS = 0x01

FLAG_SOFTENED = 0x01
FLAG_HAS_DEBUG = 0x02
FLAG_COMPRESSED = 0x04


def opcode_set_hash_bytes():
    """
    Return 16-byte digest of opcodes.incl for header invalidation.

    :return: bytes of length 16 (MD5 per kio-format.md)
    """

    pkg = Path(__file__).resolve().parent.parent
    incl = pkg / 'lib' / 'opcodes.incl'
    data = incl.read_bytes()
    return hashlib.md5(data).digest()


# The end.
