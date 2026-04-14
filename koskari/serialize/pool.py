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
koskari.serialize.pool

Constant pool encoding and decoding for .kio files.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


import struct

from koskari.lib import (
    NamespaceSpec,
    keyword,
    nil,
    symbol,
)


K_NIL = 1
K_TRUE = 2
K_FALSE = 3
K_NONE = 4
K_INT = 5
K_FLOAT = 6
K_COMPLEX = 7
K_STR = 8
K_BYTES = 9
K_SYMBOL = 10
K_KEYWORD = 11
K_TUPLE = 12
K_CODE_REF = 14
K_NSPC_REF = 15
K_FRMS_REF = 16
K_NAMESPACE_SPEC = 17


def _pack_string(s):
    b = s.encode('utf-8')
    return struct.pack('<I', len(b)) + b


def _unpack_string(buf, off):
    ln = struct.unpack_from('<I', buf, off)[0]
    off += 4
    s = buf[off:off + ln].decode('utf-8')
    return s, off + ln


def _pack_int_big(n):
    if n == 0:
        nb = 1
        payload = b'\x00'
    else:
        payload = n.to_bytes((n.bit_length() + 7 + (n < 0)) // 8, 'big', signed=True)
        nb = len(payload)
    return struct.pack('<I', nb) + payload


def _unpack_int_big(buf, off):
    nb = struct.unpack_from('<I', buf, off)[0]
    off += 4
    payload = buf[off:off + nb]
    off += nb
    return int.from_bytes(payload, 'big', signed=True), off


class PoolBuilder(object):
    """
    Identity-based pool builder for serialization.

    :ivar values: list of values in pool order
    :ivar _index: id(obj) -> pool index
    """

    def __init__(self):
        self.values = []
        self._index = {}

    def add(self, obj):
        """
        Insert value if missing, return pool index.

        :param obj: allowed constant value
        :return: pool index
        """

        i = id(obj)
        if i in self._index:
            return self._index[i]
        idx = len(self.values)
        self._index[i] = idx
        self.values.append(obj)
        return idx


def pool_record_encode(val, index_of):
    """
    Encode one pool entry to bytes.

    :param val: stored value
    :param index_of: callable(obj) -> pool index for nested tuple elements
    :return: bytes
    """

    if isinstance(val, tuple) and len(val) == 2 and val[0] == 'NSPC_REF':
        return struct.pack('<BI', K_NSPC_REF, val[1])
    if isinstance(val, tuple) and len(val) == 2 and val[0] == 'FRMS_REF':
        return struct.pack('<BI', K_FRMS_REF, val[1])
    if isinstance(val, tuple) and len(val) == 2 and val[0] == 'CODE_REF':
        return struct.pack('<BI', K_CODE_REF, val[1])
    if isinstance(val, NamespaceSpec):
        ni = index_of(val.name)
        li = index_of(tuple(val.local_syms))
        ci = index_of(tuple(val.closure_syms))
        gi = index_of(tuple(val.global_syms))
        return struct.pack(
            '<BIIII', K_NAMESPACE_SPEC, ni, li, ci, gi)

    if val is nil:
        return struct.pack('<B', K_NIL)
    if val is True:
        return struct.pack('<B', K_TRUE)
    if val is False:
        return struct.pack('<B', K_FALSE)
    if val is None:
        return struct.pack('<B', K_NONE)
    if isinstance(val, int):
        return struct.pack('<B', K_INT) + _pack_int_big(val)
    if isinstance(val, float):
        return struct.pack('<Bd', K_FLOAT, val)
    if isinstance(val, complex):
        return struct.pack('<Bdd', K_COMPLEX, val.real, val.imag)
    if isinstance(val, str):
        return struct.pack('<B', K_STR) + _pack_string(val)
    if isinstance(val, bytes):
        return struct.pack('<B', K_BYTES) + struct.pack(
            '<I', len(val)) + val
    if isinstance(val, type(symbol('x'))):
        return struct.pack('<B', K_SYMBOL) + _pack_string(str(val))
    if isinstance(val, type(keyword('x'))):
        return struct.pack('<B', K_KEYWORD) + _pack_string(str(val))
    if isinstance(val, tuple):
        out = bytearray()
        out.append(K_TUPLE)
        out.extend(struct.pack('<I', len(val)))
        for elem in val:
            out.extend(struct.pack('<I', index_of(elem)))
        return bytes(out)
    raise TypeError('unsupported pool value %r' % (val,))


def encode_pool(pool_values, index_of):
    """
    Encode full POOL section body (count + records).

    :param pool_values: list of values in index order
    :param index_of: callable for tuple element refs
    :return: bytes
    """

    chunks = [struct.pack('<I', len(pool_values))]
    for val in pool_values:
        chunks.append(pool_record_encode(val, index_of))
    return b''.join(chunks)


def decode_pool_records(buf, off, nspec_list, frms_list, code_list):
    """
    Decode POOL section at off (starts with u32 count).

    :param buf: file bytes
    :param off: offset to count
    :param nspec_list: NamespaceSpec by NSPC index
    :param frms_list: FormalsSpec by FRMS index
    :param code_list: Code by CODE body index (may be placeholder length)
    :return: tuple (pool list, new offset)
    """

    count = struct.unpack_from('<I', buf, off)[0]
    off += 4
    out = []
    for _ in range(count):
        kind = buf[off]
        off += 1
        if kind == K_NIL:
            out.append(nil)
        elif kind == K_TRUE:
            out.append(True)
        elif kind == K_FALSE:
            out.append(False)
        elif kind == K_NONE:
            out.append(None)
        elif kind == K_INT:
            v, off = _unpack_int_big(buf, off)
            out.append(v)
        elif kind == K_FLOAT:
            v = struct.unpack_from('<d', buf, off)[0]
            off += 8
            out.append(v)
        elif kind == K_COMPLEX:
            r, i = struct.unpack_from('<dd', buf, off)
            off += 16
            out.append(complex(r, i))
        elif kind == K_STR:
            s, off = _unpack_string(buf, off)
            out.append(s)
        elif kind == K_BYTES:
            ln = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(buf[off:off + ln])
            off += ln
        elif kind == K_SYMBOL:
            s, off = _unpack_string(buf, off)
            out.append(symbol(s))
        elif kind == K_KEYWORD:
            s, off = _unpack_string(buf, off)
            out.append(keyword(s))
        elif kind == K_TUPLE:
            arity = struct.unpack_from('<I', buf, off)[0]
            off += 4
            idxs = struct.unpack_from('<' + 'I' * arity, buf, off)
            off += 4 * arity
            out.append(('__tuple__', idxs))
        elif kind == K_CODE_REF:
            bi = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(('__code__', bi))
        elif kind == K_NSPC_REF:
            ni = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(nspec_list[ni])
        elif kind == K_FRMS_REF:
            fi = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(frms_list[fi])
        elif kind == K_NAMESPACE_SPEC:
            ni, li, ci, gi = struct.unpack_from('<IIII', buf, off)
            off += 16
            out.append(('__nspec__', ni, li, ci, gi))
        else:
            raise ValueError('unknown pool kind %s' % kind)

    for i, val in enumerate(out):
        if (
                isinstance(val, tuple) and len(val) == 2
                and val[0] == '__code__'):
            out[i] = code_list[val[1]]

    changed = True
    while changed:
        changed = False
        for i, val in enumerate(out):
            if (
                    isinstance(val, tuple) and len(val) == 2
                    and val[0] == '__tuple__'):
                idxs = val[1]
                try:
                    out[i] = tuple(out[j] for j in idxs)
                    changed = True
                except Exception:
                    pass

    for i, val in enumerate(out):
        if (
                isinstance(val, tuple) and len(val) == 5
                and val[0] == '__nspec__'):
            ni, li, ci, gi = val[1], val[2], val[3], val[4]
            out[i] = NamespaceSpec(
                out[ni], out[li], out[ci], out[gi])

    return out, off


# The end.
