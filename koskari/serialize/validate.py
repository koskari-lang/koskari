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
koskari.serialize.validate

Strict validation helpers for untrusted .kio payloads.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


def expect_u32(name, val):
    """
    :param name: field name for errors
    :param val: value to check
    :raises ValueError: if not in 0..2**32-1
    """

    if val < 0 or val > 0xFFFFFFFF:
        raise ValueError('%s out of u32 range: %r' % (name, val))


def expect_range(name, val, lo, hi):
    """
    :param name: field name
    :param val: value
    :param lo: inclusive low
    :param hi: inclusive high
    :raises ValueError: if val not in range
    """

    if val < lo or val > hi:
        raise ValueError('%s out of range [%s,%s]: %r' % (name, lo, hi, val))


# The end.
