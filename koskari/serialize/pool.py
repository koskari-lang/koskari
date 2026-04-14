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
from enum import IntEnum

from koskari.lib import (
    FormalsSpec,
    FunctionSpec,
    NamespaceSpec,
    OpCode,
    keyword,
    nil,
    symbol,
)

from koskari.serialize.sections import decode_instruction, encode_softened_instructions
from koskari.serialize.softcode import reharden_code, soften_code, validate_fast_indices


class PoolKind(IntEnum):
    """
    Leading byte for each encoded POOL record (on-wire discriminator)
    """

    NIL = 1
    TRUE = 2
    FALSE = 3
    NONE = 4
    INT = 5
    FLOAT = 6
    COMPLEX = 7
    STR = 8
    BYTES = 9
    SYMBOL = 10
    KEYWORD = 11
    TUPLE = 12
    CODE_REF = 14
    NSPC_REF = 15
    FRMS_REF = 16
    NAMESPACE_SPEC = 17
    FORMALS_SPEC = 18
    FUNCTION_SPEC = 19


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


def pool_record_encode(val, index_of, pb=None):
    """
    Encode one pool entry to bytes.

    :param val: stored value
    :param index_of: callable(obj) -> pool index for nested tuple elements
    :param pb: optional PoolBuilder (required for FunctionSpec)
    :return: bytes
    """

    if isinstance(val, tuple) and len(val) == 2 and val[0] == 'NSPC_REF':
        return struct.pack('<BI', PoolKind.NSPC_REF, val[1])
    if isinstance(val, tuple) and len(val) == 2 and val[0] == 'FRMS_REF':
        return struct.pack('<BI', PoolKind.FRMS_REF, val[1])
    if isinstance(val, tuple) and len(val) == 2 and val[0] == 'CODE_REF':
        return struct.pack('<BI', PoolKind.CODE_REF, val[1])
    if isinstance(val, NamespaceSpec):
        ni = index_of(val.name)
        li = index_of(tuple(val.local_syms))
        ci = index_of(tuple(val.closure_syms))
        gi = index_of(tuple(val.global_syms))
        return struct.pack(
            '<BIIII', PoolKind.NAMESPACE_SPEC, ni, li, ci, gi)

    if isinstance(val, FormalsSpec):
        idxs = (
            index_of(val.pos),
            index_of(val.pos_only),
            index_of(val.kwd),
            index_of(val.kwd_only),
            index_of(val.star),
            index_of(val.starstar),
            index_of(val.annotations),
        )
        return struct.pack('<BIIIIIII', PoolKind.FORMALS_SPEC, *idxs)

    if isinstance(val, FunctionSpec):
        if pb is None:
            raise TypeError(
                'FunctionSpec pool encoding requires a PoolBuilder')

        code = val.code
        instr, lc = soften_code(code)
        yld = False
        for op, _ in instr:
            if op in (OpCode.KOS_OP_YIELD, OpCode.KOS_OP_YIELD_FROM):
                yld = True
                break
        flg = 0x01 if yld else 0
        instr_b = encode_softened_instructions(pb, instr, lc)
        ni = index_of(val.name)
        fi = index_of(val.formals_spec)
        nsi = index_of(code.namespace_spec)
        cni = index_of(code.name)
        cfi = index_of(code.filename)
        ic = len(instr)
        return struct.pack(
            '<BIIIIIIII',
            PoolKind.FUNCTION_SPEC,
            ni,
            fi,
            nsi,
            cni,
            cfi,
            flg,
            ic,
            lc) + instr_b

    if val is nil:
        return struct.pack('<B', PoolKind.NIL)
    if val is True:
        return struct.pack('<B', PoolKind.TRUE)
    if val is False:
        return struct.pack('<B', PoolKind.FALSE)
    if val is None:
        return struct.pack('<B', PoolKind.NONE)
    if isinstance(val, int):
        return struct.pack('<B', PoolKind.INT) + _pack_int_big(val)
    if isinstance(val, float):
        return struct.pack('<Bd', PoolKind.FLOAT, val)
    if isinstance(val, complex):
        return struct.pack('<Bdd', PoolKind.COMPLEX, val.real, val.imag)
    if isinstance(val, str):
        return struct.pack('<B', PoolKind.STR) + _pack_string(val)
    if isinstance(val, bytes):
        return struct.pack('<B', PoolKind.BYTES) + struct.pack(
            '<I', len(val)) + val
    if isinstance(val, type(symbol('x'))):
        return struct.pack('<B', PoolKind.SYMBOL) + _pack_string(str(val))
    if isinstance(val, type(keyword('x'))):
        return struct.pack('<B', PoolKind.KEYWORD) + _pack_string(str(val))
    if isinstance(val, tuple):
        out = bytearray()
        out.append(PoolKind.TUPLE)
        out.extend(struct.pack('<I', len(val)))
        for elem in val:
            out.extend(struct.pack('<I', index_of(elem)))
        return bytes(out)
    raise TypeError('unsupported pool value %r' % (val,))


def encode_pool(pool_values, index_of, pb=None):
    """
    Encode full POOL section body (count + records).

    :param pool_values: list of values in index order
    :param index_of: callable for tuple element refs
    :param pb: optional PoolBuilder (required when values include FunctionSpec)
    :return: bytes
    """

    chunks = [struct.pack('<I', len(pool_values))]
    for val in pool_values:
        chunks.append(pool_record_encode(val, index_of, pb))
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
        if kind == PoolKind.NIL:
            out.append(nil)
        elif kind == PoolKind.TRUE:
            out.append(True)
        elif kind == PoolKind.FALSE:
            out.append(False)
        elif kind == PoolKind.NONE:
            out.append(None)
        elif kind == PoolKind.INT:
            v, off = _unpack_int_big(buf, off)
            out.append(v)
        elif kind == PoolKind.FLOAT:
            v = struct.unpack_from('<d', buf, off)[0]
            off += 8
            out.append(v)
        elif kind == PoolKind.COMPLEX:
            r, i = struct.unpack_from('<dd', buf, off)
            off += 16
            out.append(complex(r, i))
        elif kind == PoolKind.STR:
            s, off = _unpack_string(buf, off)
            out.append(s)
        elif kind == PoolKind.BYTES:
            ln = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(buf[off:off + ln])
            off += ln
        elif kind == PoolKind.SYMBOL:
            s, off = _unpack_string(buf, off)
            out.append(symbol(s))
        elif kind == PoolKind.KEYWORD:
            s, off = _unpack_string(buf, off)
            out.append(keyword(s))
        elif kind == PoolKind.TUPLE:
            arity = struct.unpack_from('<I', buf, off)[0]
            off += 4
            idxs = struct.unpack_from('<' + 'I' * arity, buf, off)
            off += 4 * arity
            out.append(('__tuple__', idxs))
        elif kind == PoolKind.CODE_REF:
            bi = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(('__code__', bi))
        elif kind == PoolKind.NSPC_REF:
            ni = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(nspec_list[ni])
        elif kind == PoolKind.FRMS_REF:
            fi = struct.unpack_from('<I', buf, off)[0]
            off += 4
            out.append(frms_list[fi])
        elif kind == PoolKind.NAMESPACE_SPEC:
            ni, li, ci, gi = struct.unpack_from('<IIII', buf, off)
            off += 16
            out.append(('__nspec__', ni, li, ci, gi))
        elif kind == PoolKind.FORMALS_SPEC:
            idxs = struct.unpack_from('<IIIIIII', buf, off)
            off += 28
            out.append(('__formals__', idxs))
        elif kind == PoolKind.FUNCTION_SPEC:
            ni, fi, nsi, cni, cfi, flg, ic, lc = struct.unpack_from(
                '<IIIIIIII', buf, off)
            off += 32
            ib = buf[off:off + ic * 8]
            off += ic * 8
            out.append((
                '__fspec__', ni, fi, nsi, cni, cfi, flg, ic, lc, ib))
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

    for i, val in enumerate(out):
        if (
                isinstance(val, tuple) and len(val) == 2
                and val[0] == '__formals__'):
            idxs = val[1]
            out[i] = FormalsSpec(*[out[j] for j in idxs])

    for i, val in enumerate(out):
        if (
                isinstance(val, tuple) and len(val) == 10
                and val[0] == '__fspec__'):
            _, ni, fi, nsi, cni, cfi, _flg, ic, lc, ib = val
            name = out[ni]
            formals = out[fi]
            nspec = out[nsi]
            code_name = out[cni]
            code_fn = out[cfi]
            instr = []
            ioff = 0
            for _ in range(ic):
                (op, arg), ioff = decode_instruction(ib, ioff, out)
                instr.append((op, arg))
            validate_fast_indices(nspec, instr)
            code = reharden_code(nspec, instr, code_name, code_fn)
            out[i] = FunctionSpec(name, formals, code)

    return out, off


# The end.
