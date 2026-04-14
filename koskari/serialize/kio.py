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
koskari.serialize.kio

Read and write .kio sectioned files.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


from koskari.lib import NamespaceSpec, OpCode, OpStyle

from koskari.serialize.constants import (
    FINALIZE_PIPELINE_VERSION,
    FORMAT_VERSION,
    FLAG_SOFTENED,
    STRUCT_LAYOUT_VERSIONS,
    opcode_set_hash_bytes,
)
from koskari.serialize.softcode import validate_fast_indices
from koskari.serialize.pool import decode_pool_records, encode_pool
from koskari.serialize.pool import PoolBuilder
from koskari.serialize.sections import (
    CODEP_TAG,
    FRMS_TAG,
    META_TAG,
    NSPC_TAG,
    POOL_TAG,
    decode_codep_bodies,
    decode_directory,
    decode_instruction,
    decode_meta,
    encode_codep_bodies,
    encode_directory,
    encode_frms_section,
    encode_meta,
    encode_instruction,
    encode_nspec_section,
    pack_header,
    unpack_header,
)


def _intern_for_pool(pb, obj):
    """
    Insert object into pool with dependency order for NamespaceSpec and tuples.

    :param pb: PoolBuilder
    :param obj: value to intern
    :return: pool index
    """

    if id(obj) in pb._index:
        return pb._index[id(obj)]
    if isinstance(obj, NamespaceSpec):
        _intern_for_pool(pb, obj.name)
        _intern_for_pool(pb, tuple(obj.local_syms))
        _intern_for_pool(pb, tuple(obj.closure_syms))
        _intern_for_pool(pb, tuple(obj.global_syms))
        idx = len(pb.values)
        pb._index[id(obj)] = idx
        pb.values.append(obj)
        return idx
    if isinstance(obj, tuple):
        for x in obj:
            _intern_for_pool(pb, x)
        idx = len(pb.values)
        pb._index[id(obj)] = idx
        pb.values.append(obj)
        return idx
    idx = len(pb.values)
    pb._index[id(obj)] = idx
    pb.values.append(obj)
    return idx


def _intern_instruction_args(pb, instructions):
    """
    Intern all CONST, VAR, and EXTRA operands referenced by instructions.

    :param pb: PoolBuilder
    :param instructions: softened instruction list
    """

    for op, arg in instructions:
        op = op if isinstance(op, OpCode) else OpCode(op)
        st = getattr(op.style, 'value', op.style)
        if st == OpStyle.KOSTYLE_CONST:
            _intern_for_pool(pb, arg)
        elif st == OpStyle.KOSTYLE_VAR:
            _intern_for_pool(pb, arg)
        elif st == OpStyle.KOSTYLE_EXTRA:
            _intern_for_pool(pb, arg)


def build_codep_instr_bytes(pb, instructions, label_count):
    """
    Encode softened instructions to CODEP instruction blob.

    :param pb: PoolBuilder with args already interned
    :param instructions: softened instructions
    :param label_count: number of distinct labels
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


def dump_kio(path, bodies, soft_meta=None):
    """
    Write a .kio file from a list of code bodies.

    Each body dict: nspec (NamespaceSpec), name (str), filename (str),
    instructions (softened list), label_count (int), yields (bool).

    :param path: output file path
    :param bodies: list of body dicts
    :param soft_meta: optional bytes for META tail
    """

    pb = PoolBuilder()
    for body in bodies:
        _intern_for_pool(pb, body['name'])
        _intern_for_pool(pb, body['filename'])
        _intern_for_pool(pb, body['nspec'])
        _intern_instruction_args(pb, body['instructions'])

    nspec_rows = []
    frms_rows = []
    codep_payload = []

    for body in bodies:
        flg = 0x01 if body.get('yields') else 0
        lc = body.get('label_count', 0)
        instr_b = build_codep_instr_bytes(
            pb, body['instructions'], lc)
        codep_payload.append({
            'name_idx': _intern_for_pool(pb, body['name']),
            'filename_idx': _intern_for_pool(pb, body['filename']),
            'nspec_idx': _intern_for_pool(pb, body['nspec']),
            'flags': flg,
            'instr_count': len(body['instructions']),
            'label_count': lc,
            'instr_bytes': instr_b,
        })

    def index_of(o):
        return pb._index[id(o)]

    pool_bytes = encode_pool(pb.values, index_of)

    codep_bytes = encode_codep_bodies(codep_payload)
    nspec_bytes = encode_nspec_section(nspec_rows)
    frms_bytes = encode_frms_section(frms_rows)

    hard_meta = (
        FORMAT_VERSION,
        opcode_set_hash_bytes(),
        FINALIZE_PIPELINE_VERSION,
        STRUCT_LAYOUT_VERSIONS,
    )
    meta_bytes = encode_meta(hard_meta, soft_meta)

    sections = [
        (POOL_TAG, pool_bytes),
        (NSPC_TAG, nspec_bytes),
        (FRMS_TAG, frms_bytes),
        (CODEP_TAG, codep_bytes),
        (META_TAG, meta_bytes),
    ]

    body = bytearray()
    offset = 50
    directory = []
    for tag, data in sections:
        directory.append((tag, offset, len(data)))
        body.extend(data)
        offset += len(data)

    dir_off = len(body) + 50
    dir_bytes = encode_directory(directory)
    body.extend(dir_bytes)

    header = pack_header(
        FORMAT_VERSION,
        hard_meta[1],
        FINALIZE_PIPELINE_VERSION,
        STRUCT_LAYOUT_VERSIONS,
        FLAG_SOFTENED,
        dir_off,
        len(directory),
    )

    if len(header) != 50:
        raise RuntimeError('header size mismatch %s' % len(header))

    with open(path, 'wb') as f:
        f.write(header)
        f.write(body)


def load_kio(path):
    """
    Load .kio file and return decoded payload.

    :param path: file path
    :return: dict with pool, bodies (instructions per body; FAST validated)
    """

    with open(path, 'rb') as f:
        data = f.read()

    magic, fv, oh, fin, sv, flags, dir_off, dir_cnt = unpack_header(data)
    if magic != b'KIO\x00\x00\x00\x00\x00':
        raise ValueError('bad .kio magic')
    if fv != FORMAT_VERSION:
        raise ValueError('unsupported .kio format version %s' % fv)
    if oh != opcode_set_hash_bytes():
        raise ValueError('.kio opcode set hash does not match this interpreter')
    if fin != FINALIZE_PIPELINE_VERSION:
        raise ValueError('unsupported finalize pipeline version %s' % fin)
    if sv != STRUCT_LAYOUT_VERSIONS:
        raise ValueError('unsupported struct layout version %s' % sv)

    entries = decode_directory(data, dir_off, dir_cnt)
    sec = {e[0]: (e[1], e[2]) for e in entries}

    nspec_list = []
    frms_list = []
    code_list = []

    if sec[NSPC_TAG][1] > 0:
        from koskari.serialize.sections import decode_nspec_section
        nspec_list, _ = decode_nspec_section(
            data, sec[NSPC_TAG][0], [])

    if sec[FRMS_TAG][1] > 0:
        from koskari.serialize.sections import decode_frms_section
        frms_list, _ = decode_frms_section(
            data, sec[FRMS_TAG][0], [])

    pool, _ = decode_pool_records(
        data, sec[POOL_TAG][0], nspec_list, frms_list, code_list)

    bodies_meta, _ = decode_codep_bodies(data, sec[CODEP_TAG][0])

    meta_hard, meta_soft, _ = decode_meta(
        data, sec[META_TAG][0])

    bodies = []
    for bm in bodies_meta:
        ib = bm['instr_bytes']
        ic = bm['instr_count']
        instr = []
        ioff = 0
        for _ in range(ic):
            (op, arg), ioff = decode_instruction(ib, ioff, pool)
            instr.append((op, arg))
        nspec = pool[bm['nspec_idx']]
        name = pool[bm['name_idx']]
        filename = pool[bm['filename_idx']]
        yields = bool(bm['flags'] & 0x01)
        validate_fast_indices(nspec, instr)
        bodies.append({
            'nspec': nspec,
            'name': name,
            'filename': filename,
            'instructions': instr,
            'yields': yields,
        })

    return {
        'format_version': fv,
        'flags': flags,
        'meta_hard': meta_hard,
        'meta_soft': meta_soft,
        'pool': pool,
        'bodies': bodies,
    }


# The end.
