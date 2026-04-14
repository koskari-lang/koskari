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
koskari.serialize.sections

Binary sections for .kio files.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


import struct

from koskari.lib import OpCode, OpStyle

from koskari.serialize.constants import KIO_MAGIC


HEADER_SIZE = struct.calcsize('<8sH16sIIIQI')

ARG_NONE = 0
ARG_U32 = 1
ARG_LABEL_ID = 2
ARG_POOL_INDEX = 3

POOL_TAG = b'POOL'
NSPC_TAG = b'NSPC'
FRMS_TAG = b'FRMS'
# Spec names this the CODEP section; on-wire tags are exactly 4 bytes
CODEP_TAG = b'CODE'
META_TAG = b'META'

DIR_ENTRY_SIZE = struct.calcsize('<4sQQ')


def pack_header(
        format_version,
        opcode_hash_16,
        finalize_ver,
        struct_ver,
        flags,
        section_dir_offset,
        section_dir_count):
    """
    Pack fixed .kio header.

    :return: bytes of length HEADER_SIZE
    """

    return struct.pack(
        '<8sH16sIIIQI',
        KIO_MAGIC,
        format_version,
        opcode_hash_16,
        finalize_ver,
        struct_ver,
        flags,
        section_dir_offset,
        section_dir_count)


def unpack_header(buf):
    """
    Unpack header from start of file.

    :return: tuple of fields
    """

    return struct.unpack_from('<8sH16sIIIQI', buf, 0)


def encode_directory(entries):
    """
    Encode section directory.

    :param entries: list of (tag_bytes_4, offset, length)
    :return: bytes
    """

    out = bytearray()
    for tag, off, ln in entries:
        out.extend(struct.pack('<4sQQ', tag, off, ln))
    return bytes(out)


def decode_directory(buf, off, count):
    """
    Decode section directory at offset.

    :return: list of (tag, offset, length)
    """

    entries = []
    for _ in range(count):
        tag, o, ln = struct.unpack_from('<4sQQ', buf, off)
        off += DIR_ENTRY_SIZE
        entries.append((tag, o, ln))
    return entries


def label_id_from_name(name):
    """
    Parse L{n} label name to integer id.

    :param name: label string like L0
    :return: integer
    """

    if isinstance(name, str) and name.startswith('L'):
        rest = name[1:]
        if rest.isdigit():
            return int(rest)
    raise ValueError('bad label name %r' % (name,))


def label_name_from_id(lid):
    """
    Format label id as L{n}.

    :param lid: integer label id
    :return: str
    """

    return 'L%d' % lid


def encode_instruction(op, arg, pool_index_of, label_count):
    """
    Encode one softened instruction to 8 bytes.

    :param op: OpCode
    :param arg: softened argument
    :param pool_index_of: callable(value) -> pool index
    :param label_count: max label id + 1 for validation
    :return: bytes
    """

    op_int = op.value if hasattr(op, 'value') else int(op)
    st = getattr(op.style, 'value', op.style)

    if op == OpCode.KOS_OP_POSITION:
        line, col = arg[0], arg[1]
        packed = ((line & 0xFFFF) << 16) | (col & 0xFFFF)
        return struct.pack('<HBBI', op_int, ARG_U32, 0, packed)

    if op == OpCode.KOS_OP_LABEL:
        lid = label_id_from_name(arg)
        return struct.pack('<HBBI', op_int, ARG_LABEL_ID, 0, lid)

    if st == OpStyle.KOSTYLE_CONST:
        idx = pool_index_of(arg)
        return struct.pack('<HBBI', op_int, ARG_POOL_INDEX, 0, idx)

    if st == OpStyle.KOSTYLE_VAR:
        idx = pool_index_of(arg)
        return struct.pack('<HBBI', op_int, ARG_POOL_INDEX, 0, idx)

    if st == OpStyle.KOSTYLE_LABEL:
        lid = label_id_from_name(arg)
        return struct.pack('<HBBI', op_int, ARG_LABEL_ID, 0, lid)

    if st == OpStyle.KOSTYLE_EXTRA:
        idx = pool_index_of(arg)
        return struct.pack('<HBBI', op_int, ARG_POOL_INDEX, 0, idx)

    if st in (OpStyle.KOSTYLE_NOARG,):
        return struct.pack('<HBBI', op_int, ARG_NONE, 0, 0)

    if st in (OpStyle.KOSTYLE_COUNT, OpStyle.KOSTYLE_PAIR_COUNT,
              OpStyle.KOSTYLE_STAR, OpStyle.KOSTYLE_FASTVAR):
        return struct.pack('<HBBI', op_int, ARG_U32, 0, int(arg) & 0xFFFFFFFF)

    return struct.pack('<HBBI', op_int, ARG_U32, 0, int(arg) & 0xFFFFFFFF)


def decode_instruction(buf, off, pool_list):
    """
    Decode one instruction record to (OpCode, arg).

    :param buf: bytes
    :param off: offset
    :param pool_list: decoded pool list for pool indices
    :return: tuple ((OpCode, arg), new_offset)
    """

    op_int, arg_kind, _pad, arg_val = struct.unpack_from('<HBBI', buf, off)
    off += 8
    op = OpCode(op_int)
    st = getattr(op.style, 'value', op.style)

    if op == OpCode.KOS_OP_POSITION:
        line = (arg_val >> 16) & 0xFFFF
        col = arg_val & 0xFFFF
        return (op, (line, col)), off

    if op == OpCode.KOS_OP_LABEL:
        return (op, label_name_from_id(arg_val)), off

    if arg_kind == ARG_POOL_INDEX:
        val = pool_list[arg_val]
        if st == OpStyle.KOSTYLE_EXTRA:
            return (op, val), off
        return (op, val), off

    if arg_kind == ARG_LABEL_ID:
        return (op, label_name_from_id(arg_val)), off

    if arg_kind == ARG_NONE:
        return (op, 0), off

    if arg_kind == ARG_U32:
        if st == OpStyle.KOSTYLE_FASTVAR:
            return (op, arg_val), off
        return (op, arg_val), off

    return (op, arg_val), off


def encode_softened_instructions(pb, instructions, label_count):
    """
    Encode a softened instruction list to CODEP-style bytes (8 bytes each).

    Every operand that uses pool indices must already be interned in
    pb._index.

    :param pb: PoolBuilder with interned values
    :param instructions: list of (OpCode, arg) pairs
    :param label_count: number of distinct labels for jump validation
    :return: bytes
    """

    def index_of(obj):
        i = id(obj)
        if i not in pb._index:
            raise ValueError('pool index missing for %r' % (obj,))
        return pb._index[i]

    chunks = []
    for op, arg in instructions:
        op = op if isinstance(op, OpCode) else OpCode(op)
        chunks.append(
            encode_instruction(op, arg, index_of, label_count))
    return b''.join(chunks)


def encode_nspec_section(nspec_entries):
    """
    Encode NSPC section: count + rows of u32 indices.

    Each row: name_pool_idx, local_tuple_idx, closure_tuple_idx, global_tuple_idx

    :param nspec_entries: list of dict with keys name_idx, local_idx, closure_idx, global_idx
    :return: bytes
    """

    chunks = [struct.pack('<I', len(nspec_entries))]
    for e in nspec_entries:
        chunks.append(struct.pack(
            '<IIII',
            e['name_idx'],
            e['local_idx'],
            e['closure_idx'],
            e['global_idx']))
    return b''.join(chunks)


def decode_nspec_section(buf, off, pool):
    """
    Build NamespaceSpec list from section bytes.

    :return: tuple (list of NamespaceSpec, new offset)
    """

    from koskari.lib import NamespaceSpec

    count = struct.unpack_from('<I', buf, off)[0]
    off += 4
    out = []
    for _ in range(count):
        ni, li, ci, gi = struct.unpack_from('<IIII', buf, off)
        off += 16
        name = pool[ni]
        loc = pool[li]
        clo = pool[ci]
        glo = pool[gi]
        out.append(NamespaceSpec(name, loc, clo, glo))
    return out, off


def encode_frms_section(frms_entries):
    """
    Encode FormalsSpec section as count + rows of 7 pool indices.

    :param frms_entries: list of 7-tuples of pool indices
    :return: bytes
    """

    chunks = [struct.pack('<I', len(frms_entries))]
    for row in frms_entries:
        if len(row) != 7:
            raise ValueError('Formals row must have 7 indices')
        chunks.append(struct.pack('<IIIIIII', *row))
    return b''.join(chunks)


def decode_frms_section(buf, off, pool):
    """
    Build FormalsSpec list.

    :return: tuple (list, new offset)
    """

    from koskari.lib import FormalsSpec

    count = struct.unpack_from('<I', buf, off)[0]
    off += 4
    out = []
    for _ in range(count):
        idxs = struct.unpack_from('<IIIIIII', buf, off)
        off += 28
        args = [pool[i] for i in idxs]
        out.append(FormalsSpec(*args))
    return out, off


def encode_codep_bodies(bodies):
    """
    Encode CODEP section: count + each body.

    :param bodies: list of dict with keys name_idx, filename_idx, nspec_idx,
        flags, instr_bytes, label_count
    :return: bytes
    """

    chunks = [struct.pack('<I', len(bodies))]
    for b in bodies:
        chunks.append(struct.pack(
            '<IIIIII',
            b['name_idx'],
            b['filename_idx'],
            b['nspec_idx'],
            b['flags'],
            b['instr_count'],
            b['label_count']))
        chunks.append(b['instr_bytes'])
    return b''.join(chunks)


def decode_codep_bodies(buf, off):
    """
    Decode CODEP bodies; returns metadata and raw instr blob per body.

    :return: tuple (list of dict, new offset)
    """

    count = struct.unpack_from('<I', buf, off)[0]
    off += 4
    out = []
    for _ in range(count):
        name_idx, fn_idx, ns_idx, flags, ic, lc = struct.unpack_from(
            '<IIIIII', buf, off)
        off += 24
        ib = buf[off:off + ic * 8]
        off += ic * 8
        out.append({
            'name_idx': name_idx,
            'filename_idx': fn_idx,
            'nspec_idx': ns_idx,
            'flags': flags,
            'instr_count': ic,
            'label_count': lc,
            'instr_bytes': ib,
        })
    return out, off


def encode_meta(hard_meta, soft_meta=None):
    """
    Encode META section (hard invalidators + optional soft blob).

    :param hard_meta: tuple (format_version, opcode_hash_16, finalize_ver, struct_ver)
    :param soft_meta: optional bytes (opaque)
    :return: bytes
    """

    fv, oh, fin, sv = hard_meta
    soft = soft_meta or b''
    return struct.pack('<H16sII', fv, oh, fin, sv) + struct.pack(
        '<I', len(soft)) + soft


def decode_meta(buf, off):
    """
    Decode META section.

    :return: tuple (hard_tuple, soft_bytes, new_off)
    """

    fv, oh, fin, sv = struct.unpack_from('<H16sII', buf, off)
    off += 28
    sln = struct.unpack_from('<I', buf, off)[0]
    off += 4
    soft = buf[off:off + sln]
    off += sln
    return (fv, oh, fin, sv), soft, off
