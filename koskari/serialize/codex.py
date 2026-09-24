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
koskari.serialize.codex

Canonical value and record codecs for .kio serialization

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import os
import struct
from dataclasses import dataclass
from enum import Enum, IntEnum

from ..lib import (
    SOFTEN_LABEL_PREFIX, Code, FormalsSpec, FunctionSpec, NamespaceSpec,
    OpCode, OpStyle, bracket_nil, keyword, nil, symbol)
from .constants import (
    BYTECODE_ABI_MAJOR, BYTECODE_ABI_MICRO, BYTECODE_ABI_MINOR,
    BYTECODE_ABI_PATCH, KIO_MAGIC)


def binary_read_all(source):
    """
    Load raw bytes from a buffer, path, or readable binary stream

    :param source: bytes-like, filesystem path, or object with read()
    :return: bytes
    """

    if isinstance(source, (bytes, bytearray)):
        return bytes(source)
    read = getattr(source, 'read', None)
    if read is not None and callable(read):
        return read()
    if isinstance(source, (str, os.PathLike)):
        with open(os.fspath(source), 'rb') as f:
            return f.read()
    raise TypeError(
        'expected bytes, path, or binary stream, got %r' % (type(source),))


def binary_write_all(dest, data):
    """
    Write raw bytes to a path or writable binary stream

    :param dest: filesystem path or object with write(bytes)
    :param data: bytes to write
    """

    write = getattr(dest, 'write', None)
    if write is not None and callable(write):
        write(data)
        return
    if isinstance(dest, (str, os.PathLike)):
        with open(os.fspath(dest), 'wb') as f:
            f.write(data)
        return
    raise TypeError(
        'expected path or binary stream, got %r' % (type(dest),))


HEADER_SIZE = struct.calcsize('<8sIIIIIQI')
DIR_ENTRY_SIZE = struct.calcsize('<4sQQ')
INSTRUCTION_SIZE = struct.calcsize('<HBBI')


class InstructionArgKind(IntEnum):
    """
    Wire encoding for softened instruction arg_kind byte
    """

    NONE = 0
    U32 = 1
    LABEL_ID = 2
    POOL_INDEX = 3


class SectionTag(Enum):
    """
    Four-byte ASCII section tags for directory entries
    """

    POOL = 'POOL'
    CODE = 'CODE'
    META = 'META'


    def encode(self):
        return self.value.encode('ascii')


    @classmethod
    def decode(cls, tag_b):
        return cls(tag_b.decode('ascii'))


class PoolKind(IntEnum):
    """
    Leading byte for each encoded POOL record
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
    CODE_REF = 14  # reserved; nothing writes or reads it
    NAMESPACE_SPEC = 17
    FORMALS_SPEC = 18
    FUNCTION_SPEC = 19
    BRACKET_NIL = 20
    ELLIPSIS = 21
    FROZENSET = 22
    SLICE = 23


class CodeFlags(IntEnum):
    """
    Bits of the flags word on a CODE body record and a FUNCTION_SPEC pool
    record
    """

    YIELDS = 0x01
    PROTECT = 0x02
    CACHE = 0x04


def code_flags(code):
    """
    The flags word recording how ``code`` was constructed
    """

    flags = 0
    if code.yields:
        flags |= CodeFlags.YIELDS
    if code.protected:
        flags |= CodeFlags.PROTECT
    if code.caches:
        flags |= CodeFlags.CACHE
    return flags


def code_from_record(nspec, instr, name, filename, flags):
    """
    Rebuild a Code with the protect and cache settings its record stored
    """

    return Code(
        nspec, instr, name, filename,
        protect=bool(flags & CodeFlags.PROTECT),
        cache=bool(flags & CodeFlags.CACHE))


@dataclass
class SectionDirectoryEntry(object):
    """
    Logical section-directory row
    """

    tag: SectionTag
    offset: int
    length: int


    def as_tuple(self):
        return (self.tag, self.offset, self.length)


@dataclass
class CodeBodyRecord(object):
    """
    Logical CODE section record
    """

    _header = struct.Struct('<IIIII')

    name_idx: int
    filename_idx: int
    nspec_idx: int
    flags: int
    instr_count: int
    instr_bytes: bytes


    def to_bytes(self):
        """
        Serialize this record (fixed header plus instruction bytes)
        """

        return self._header.pack(
            self.name_idx,
            self.filename_idx,
            self.nspec_idx,
            self.flags,
            self.instr_count) + self.instr_bytes


    @classmethod
    def from_bytes(cls, buf, off):
        """
        Deserialize one record from a buffer at an offset

        :param buf: bytes containing the wire format
        :param off: offset of the record header
        :return: pair of record and new offset past this record
        """

        name_idx, fn_idx, ns_idx, flags, ic = cls._header.unpack_from(
            buf, off)
        off += cls._header.size
        ib = buf[off:off + ic * INSTRUCTION_SIZE]
        off += ic * INSTRUCTION_SIZE
        return cls(
            name_idx=name_idx,
            filename_idx=fn_idx,
            nspec_idx=ns_idx,
            flags=flags,
            instr_count=ic,
            instr_bytes=ib,
        ), off


@dataclass
class EncodedSections(object):
    """
    Encoded section payloads in canonical write order
    """

    pool_bytes: bytes
    codep_bytes: bytes
    meta_bytes: bytes

    def as_tagged_sequence(self):
        return [
            (SectionTag.POOL, self.pool_bytes),
            (SectionTag.CODE, self.codep_bytes),
            (SectionTag.META, self.meta_bytes),
        ]


@dataclass
class KioHeader(object):
    """
    Semantic fixed-size file header
    """

    magic: bytes
    abi_major: int
    abi_minor: int
    abi_micro: int
    abi_patch: int
    flags: int
    section_dir_offset: int
    section_dir_count: int


    @classmethod
    def default(cls):
        return cls(
            KIO_MAGIC,
            BYTECODE_ABI_MAJOR,
            BYTECODE_ABI_MINOR,
            BYTECODE_ABI_MICRO,
            BYTECODE_ABI_PATCH,
            0,
            0,
            0)


    @classmethod
    def from_bytes(cls, buf):
        magic, am, im, mic, pa, flags, dir_off, dir_cnt = struct.unpack_from(
            '<8sIIIIIQI', buf, 0)
        return cls(
            magic, am, im, mic, pa, flags, dir_off, dir_cnt)


    def validate_runtime(self):
        if self.magic != KIO_MAGIC:
            raise ValueError('bad .kio magic')

        # TODO: we may at some point allow multiple format versions. But
        # for now only the current version is supported.
        if (self.abi_major != BYTECODE_ABI_MAJOR
                or self.abi_minor != BYTECODE_ABI_MINOR
                or self.abi_micro != BYTECODE_ABI_MICRO
                or self.abi_patch != BYTECODE_ABI_PATCH):
            raise ValueError('.kio bytecode ABI does not match this interpreter')


    def to_bytes(self):
        return struct.pack(
            '<8sIIIIIQI',
            self.magic,
            self.abi_major,
            self.abi_minor,
            self.abi_micro,
            self.abi_patch,
            self.flags,
            self.section_dir_offset,
            self.section_dir_count)


@dataclass
class KioMeta(object):
    """
    META section payload

    A leading flag byte says whether a source stamp follows: the mtime
    in nanoseconds and the size in bytes of the .ki the file was
    compiled from, as recorded when it was compiled. The loader compares
    the stamp against a sibling .ki and loads the source when they
    differ or when no stamp was recorded. Any bytes after the stamp are
    soft_meta, opaque to the runtime. An empty section is a file with no
    stamp and no soft_meta.
    """

    _stamp = struct.Struct('<BqQ')

    STAMPED = 0x01

    soft_meta: bytes = b''
    source_mtime_ns: int = None
    source_size: int = None

    @classmethod
    def default(cls, soft_meta=None):
        return cls(soft_meta or b'')

    @classmethod
    def for_source(cls, path, soft_meta=None):
        """
        A META stamped from the .ki at ``path`` as it is on disk now
        """

        st = os.stat(path)
        return cls(soft_meta or b'', st.st_mtime_ns, st.st_size)

    @property
    def stamped(self):
        return self.source_mtime_ns is not None and self.source_size is not None

    @classmethod
    def from_buffer(cls, buf, off, length):
        """
        Read META body of exactly ``length`` bytes at ``off``
        """

        end = off + length
        if length == 0:
            return cls(), end

        flags = buf[off]
        off += 1
        mtime_ns = size = None
        if flags & cls.STAMPED:
            _flags, mtime_ns, size = cls._stamp.unpack_from(buf, off - 1)
            off += cls._stamp.size - 1
        return cls(bytes(buf[off:end]), mtime_ns, size), end

    def to_bytes(self):
        if self.stamped:
            head = self._stamp.pack(
                self.STAMPED, self.source_mtime_ns, self.source_size)
        elif self.soft_meta:
            head = bytes((0,))
        else:
            head = b''
        return head + (self.soft_meta or b'')


@dataclass
class KioFile(object):
    """
    Semantic representation of a .kio file
    """

    header: KioHeader
    meta: KioMeta
    codes: list
    pool: list = None


    @classmethod
    def from_codes(cls, codes, soft_meta=None, header=None, meta=None):
        if meta is None:
            meta = KioMeta.default(soft_meta=soft_meta)
        elif soft_meta is not None:
            meta.soft_meta = soft_meta
        if header is None:
            header = KioHeader.default()
        return cls(header, meta, list(codes), None)


    @classmethod
    def validate(cls, source):
        """
        Check that a .kio is structurally sound without decoding its pool
        or bodies. Returns the header and the section map, or raises
        ValueError naming the first problem found

        Checked, in order: the buffer holds a whole header; the magic and
        bytecode ABI match this interpreter; the section directory lies
        inside the file; every entry names a known tag, lies inside the
        file after the header, and appears once; the POOL, CODE, and META
        sections are all present; a stamped META is long enough to hold
        its stamp

        :param source: file path, bytes, or readable binary stream
        :return: (KioHeader, dict of SectionTag to (offset, length))
        :raises ValueError: describing the first structural fault
        """

        data = binary_read_all(source)
        size = len(data)

        if size < HEADER_SIZE:
            raise ValueError(
                f"truncated .kio: {size} bytes, header needs {HEADER_SIZE}")
        header = KioHeader.from_bytes(data)
        header.validate_runtime()

        dir_off = header.section_dir_offset
        dir_len = header.section_dir_count * DIR_ENTRY_SIZE
        if dir_off < HEADER_SIZE or dir_off + dir_len > size:
            raise ValueError(
                f".kio section directory at {dir_off} for {dir_len} bytes"
                f" lies outside the {size} byte file")

        sections = {}
        off = dir_off
        for _ in range(header.section_dir_count):
            tag_b, sec_off, sec_len = struct.unpack_from('<4sQQ', data, off)
            off += DIR_ENTRY_SIZE
            try:
                tag = SectionTag.decode(tag_b)
            except (ValueError, UnicodeDecodeError):
                raise ValueError(f"unknown .kio section tag {tag_b!r}")
            if tag in sections:
                raise ValueError(f"duplicate .kio section {tag.value}")
            if sec_off < HEADER_SIZE or sec_off + sec_len > size:
                raise ValueError(
                    f".kio section {tag.value} at {sec_off} for {sec_len}"
                    f" bytes lies outside the {size} byte file")
            sections[tag] = (sec_off, sec_len)

        for tag in (SectionTag.POOL, SectionTag.CODE, SectionTag.META):
            if tag not in sections:
                raise ValueError(f".kio has no {tag.value} section")

        meta_off, meta_len = sections[SectionTag.META]
        if meta_len and (data[meta_off] & KioMeta.STAMPED) \
                and meta_len < KioMeta._stamp.size:
            raise ValueError(
                f".kio META of {meta_len} bytes is too short for its stamp")

        return header, sections


    @classmethod
    def from_bytes(cls, data):
        header, sec = cls.validate(data)

        pool, _ = decode_pool_records(data, sec[SectionTag.POOL][0])

        code_records, _ = decode_codep_bodies(data, sec[SectionTag.CODE][0])

        meta, _off = KioMeta.from_buffer(
            data, sec[SectionTag.META][0], sec[SectionTag.META][1])

        codes = []
        for record in code_records:
            instr = []
            ioff = 0
            for _ in range(record.instr_count):
                (op, arg), ioff = decode_instruction(
                    record.instr_bytes, ioff, pool)
                instr.append((op, arg))
            nspec = pool[record.nspec_idx]
            name = pool[record.name_idx]
            filename = pool[record.filename_idx]
            code = code_from_record(
                nspec, instr, name, filename, record.flags)
            codes.append(code)

        return cls(header, meta, codes, pool)


    @classmethod
    def read(cls, source):
        return cls.from_bytes(binary_read_all(source))


    @classmethod
    def read_meta(cls, source):
        """
        The META of a .kio without decoding its pool or bodies

        :param source: file path, bytes, or readable binary stream
        :return: KioMeta
        :raises ValueError: a structural fault, bad magic, or a bytecode
            ABI this interpreter does not run (see validate)
        """

        data = binary_read_all(source)
        _header, sec = cls.validate(data)
        meta, _off = KioMeta.from_buffer(
            data, sec[SectionTag.META][0], sec[SectionTag.META][1])
        return meta


    def build_pool_and_code_records(self):
        softened = [(code, code.soften()) for code in self.codes]

        pb = PoolBuilder()
        for code, instr in softened:
            intern_for_pool(pb, code.name)
            intern_for_pool(pb, code.filename)
            intern_for_pool(pb, code.namespace_spec)
            intern_instruction_args(pb, instr)

        code_records = []
        for code, instr in softened:
            code_records.append(CodeBodyRecord(
                name_idx=intern_for_pool(pb, code.name),
                filename_idx=intern_for_pool(pb, code.filename),
                nspec_idx=intern_for_pool(pb, code.namespace_spec),
                flags=code_flags(code),
                instr_count=len(instr),
                instr_bytes=encode_softened_instructions(pb, instr),
            ))

        return pb, code_records


    def encode_sections(self):
        pb, code_records = self.build_pool_and_code_records()
        self.pool = pb.values

        return EncodedSections(
            pool_bytes=encode_pool(pb.values, pb.index_of, pb),
            codep_bytes=encode_codep_bodies(code_records),
            meta_bytes=self.meta.to_bytes(),
        )


    def to_bytes(self):
        sections = self.encode_sections()

        body = bytearray()
        offset = HEADER_SIZE
        directory = []
        for tag, data in sections.as_tagged_sequence():
            directory.append(SectionDirectoryEntry(tag, offset, len(data)))
            body.extend(data)
            offset += len(data)

        dir_off = len(body) + HEADER_SIZE
        dir_bytes = encode_directory([entry.as_tuple() for entry in directory])
        body.extend(dir_bytes)

        header = KioHeader(
            self.header.magic,
            self.header.abi_major,
            self.header.abi_minor,
            self.header.abi_micro,
            self.header.abi_patch,
            self.header.flags,
            dir_off,
            len(directory),
        )
        header_bytes = header.to_bytes()
        if len(header_bytes) != HEADER_SIZE:
            raise RuntimeError('header size mismatch %s' % len(header_bytes))

        self.header = header
        return header_bytes + bytes(body)


    def write(self, dest):
        binary_write_all(dest, self.to_bytes())


def dump_kio(dest, codes, soft_meta=None):
    """
    Write a .kio file from a list of Code objects.

    :param dest: output file path or writable binary stream
    :param codes: list of Code objects in evaluation order
    :param soft_meta: optional bytes for META tail
    """

    kio_file = KioFile.from_codes(codes, soft_meta=soft_meta)
    kio_file.write(dest)


def load_kio(source):
    """
    Load a .kio file and return a KioFile.

    :param source: file path, bytes, or readable binary stream
    :return: decoded KioFile
    """

    return KioFile.read(source)


def pool_key(obj):
    """
    The key a value shares a pool slot under. An int, str, bytes, float
    or complex value shares a slot with every equal value of the same
    type, so a constant rebuilt by the decoder and an immediate rebuilt
    by Code.soften meet in one record whether or not the interpreter
    handed back the same object. Everything else is keyed by identity

    :param obj: candidate pool value
    :return: hashable key
    """

    kind = type(obj)
    if kind in (int, str, bytes):
        return (kind, obj)
    if kind is float:
        return (kind, struct.pack('<d', obj))
    if kind is complex:
        return (kind, struct.pack('<dd', obj.real, obj.imag))
    return (None, id(obj))


class PoolBuilder(object):
    """
    Pool builder for serialization, deduplicating by pool_key

    :ivar values: list of values in pool order
    :ivar _index: pool_key(obj) -> pool index
    """

    def __init__(self):
        self.values = []
        self._index = {}
        self._softened = {}


    def soften(self, code):
        """
        The softened instruction list for code, built once per pool so
        interning and encoding see the same operand objects

        :param code: Code object
        :return: list of (op, arg) pairs
        """

        key = id(code)
        if key not in self._softened:
            self._softened[key] = (code, code.soften())
        return self._softened[key][1]


    def add(self, obj):
        """
        Insert value if missing, return pool index

        :param obj: allowed constant value
        :return: pool index
        """

        key = pool_key(obj)
        if key in self._index:
            return self._index[key]
        idx = len(self.values)
        self._index[key] = idx
        self.values.append(obj)
        return idx


    def index_of(self, obj):
        """
        Return interned pool index for object

        :param obj: interned value
        :return: pool index
        """

        key = pool_key(obj)
        if key not in self._index:
            raise ValueError(f"pool index missing for {obj!r}")
        return self._index[key]


def intern_for_pool(pb, obj):
    """
    Insert object into pool with dependency order for codec values
    """

    key = pool_key(obj)
    if key in pb._index:
        return pb._index[key]

    if isinstance(obj, NamespaceSpec):
        intern_for_pool(pb, obj.name)
        intern_for_pool(pb, tuple(obj.local_syms))
        intern_for_pool(pb, tuple(obj.closure_syms))
        intern_for_pool(pb, tuple(obj.global_syms))
        return pb.add(obj)

    if isinstance(obj, FormalsSpec):
        intern_for_pool(pb, obj.pos)
        intern_for_pool(pb, obj.pos_only)
        intern_for_pool(pb, obj.kwd)
        intern_for_pool(pb, obj.kwd_only)
        intern_for_pool(pb, obj.star)
        intern_for_pool(pb, obj.starstar)
        intern_for_pool(pb, obj.by_refs)
        return pb.add(obj)

    if isinstance(obj, FunctionSpec):
        intern_for_pool(pb, obj.name)
        intern_for_pool(pb, obj.formals_spec)
        code = obj.code
        intern_for_pool(pb, code.namespace_spec)
        intern_for_pool(pb, code.name)
        intern_for_pool(pb, code.filename)
        intern_instruction_args(pb, pb.soften(code))
        return pb.add(obj)

    if isinstance(obj, (tuple, frozenset)):
        for val in obj:
            intern_for_pool(pb, val)
        return pb.add(obj)

    if isinstance(obj, slice):
        intern_for_pool(pb, obj.start)
        intern_for_pool(pb, obj.stop)
        intern_for_pool(pb, obj.step)
        return pb.add(obj)

    return pb.add(obj)


def intern_instruction_args(pb, instructions):
    """
    Intern all CONST, VAR, and EXTRA operands for softened instructions
    """

    for op, arg in instructions:
        op = op if isinstance(op, OpCode) else OpCode(op)
        st = op.style
        if st == OpStyle.CONST:
            intern_for_pool(pb, arg)
        elif st == OpStyle.VAR:
            intern_for_pool(pb, arg)
        elif st == OpStyle.EXTRA:
            intern_for_pool(pb, arg)


def encode_directory(entries):
    """
    Encode section directory entries to bytes
    """

    out = bytearray()
    for tag, off, ln in entries:
        tag_b = tag.encode() if isinstance(tag, SectionTag) else tag
        out.extend(struct.pack('<4sQQ', tag_b, off, ln))
    return bytes(out)


def label_id_from_name(name):
    """
    Parse a softened label name back to the target pc it names. The
    name is the one Code.soften gives a jump target: the native
    SOFTEN_LABEL_PREFIX followed by the pc in decimal
    """

    if isinstance(name, str) and name.startswith(SOFTEN_LABEL_PREFIX):
        rest = name[len(SOFTEN_LABEL_PREFIX):]
        if rest.isdigit():
            return int(rest)
    raise ValueError(f"bad label name {name!r}")


def label_name_from_id(lid):
    """
    Format a target pc as the label name Code.soften would give it
    """

    return f"{SOFTEN_LABEL_PREFIX}{lid}"


def encode_instruction(op, arg, pool_index_of):
    """
    Encode one softened instruction
    """

    op_int = int(op)
    st = op.style

    if op == OpCode.POSITION:
        line, col = arg[0], arg[1]
        packed = ((line & 0xFFFF) << 16) | (col & 0xFFFF)
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.U32, 0, packed)

    if op == OpCode.LABEL:
        lid = label_id_from_name(arg)
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.LABEL_ID, 0, lid)

    if st == OpStyle.CONST:
        idx = pool_index_of(arg)
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.POOL_INDEX, 0, idx)

    if st == OpStyle.VAR:
        idx = pool_index_of(arg)
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.POOL_INDEX, 0, idx)

    if st == OpStyle.LABEL:
        lid = label_id_from_name(arg)
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.LABEL_ID, 0, lid)

    if st == OpStyle.EXTRA:
        idx = pool_index_of(arg)
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.POOL_INDEX, 0, idx)

    if st in (OpStyle.NOARG,):
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.NONE, 0, 0)

    if st in (OpStyle.COUNT, OpStyle.PAIR_COUNT,
              OpStyle.STAR, OpStyle.FASTVAR):
        return struct.pack(
            '<HBBI', op_int, InstructionArgKind.U32, 0, int(arg) & 0xFFFFFFFF)

    return struct.pack(
        '<HBBI', op_int, InstructionArgKind.U32, 0, int(arg) & 0xFFFFFFFF)


def decode_instruction(buf, off, pool_list):
    """
    Decode one instruction record
    """

    op_int, arg_kind, _pad, arg_val = struct.unpack_from('<HBBI', buf, off)
    off += INSTRUCTION_SIZE
    op = OpCode(op_int)
    st = op.style

    if op == OpCode.POSITION:
        line = (arg_val >> 16) & 0xFFFF
        col = arg_val & 0xFFFF
        return (op, (line, col)), off

    if op == OpCode.LABEL:
        return (op, label_name_from_id(arg_val)), off

    if arg_kind == InstructionArgKind.POOL_INDEX:
        val = pool_list[arg_val]
        if st == OpStyle.EXTRA:
            return (op, val), off
        return (op, val), off

    if arg_kind == InstructionArgKind.LABEL_ID:
        return (op, label_name_from_id(arg_val)), off

    if arg_kind == InstructionArgKind.NONE:
        return (op, 0), off

    if arg_kind == InstructionArgKind.U32:
        if st == OpStyle.FASTVAR:
            return (op, arg_val), off
        return (op, arg_val), off

    return (op, arg_val), off


def encode_softened_instructions(pb, instructions):
    """
    Encode softened instruction list
    """

    chunks = []
    for op, arg in instructions:
        op = op if isinstance(op, OpCode) else OpCode(op)
        chunks.append(encode_instruction(op, arg, pb.index_of))
    return b''.join(chunks)


def encode_codep_bodies(bodies):
    """
    Encode code bodies section

    :param bodies: sequence of CodeBodyRecord
    """

    chunks = [struct.pack('<I', len(bodies))]
    for body in bodies:
        chunks.append(body.to_bytes())
    return b''.join(chunks)


def decode_codep_bodies(buf, off):
    """
    Decode code bodies section

    :return: list of CodeBodyRecord and new offset
    """

    count = struct.unpack_from('<I', buf, off)[0]
    off += 4
    out = []
    for _ in range(count):
        rec, off = CodeBodyRecord.from_bytes(buf, off)
        out.append(rec)
    return out, off


def _pack_string(s):
    b = s.encode('utf-8')
    return struct.pack('<I', len(b)) + b


def _unpack_string(buf, off):
    ln = struct.unpack_from('<I', buf, off)[0]
    off += 4
    s = buf[off:off + ln].decode('utf-8')
    return s, off + ln


def _pack_int_big(n):
    """
    Two's-complement big-endian payload behind a byte count. The width
    is the magnitude's bit length plus one sign bit, rounded up to whole
    bytes; for a negative value the magnitude is the bitwise complement,
    so -128 fits one byte and -129 needs two
    """

    magnitude = n if n >= 0 else ~n
    nb = (magnitude.bit_length() + 8) // 8
    payload = n.to_bytes(nb, 'big', signed=True)
    return struct.pack('<I', nb) + payload


def _unpack_int_big(buf, off):
    nb = struct.unpack_from('<I', buf, off)[0]
    off += 4
    payload = buf[off:off + nb]
    off += nb
    return int.from_bytes(payload, 'big', signed=True), off


def encode_pool_record_value(val, index_of, pb=None):
    """
    Type-dispatch encoding for a single pool value

    :param val: value to encode
    :param index_of: pool index lookup
    :param pb: PoolBuilder when encoding FunctionSpec
    :return: encoded record bytes
    """

    if isinstance(val, NamespaceSpec):
        ni = index_of(val.name)
        li = index_of(tuple(val.local_syms))
        ci = index_of(tuple(val.closure_syms))
        gi = index_of(tuple(val.global_syms))
        return struct.pack('<BIIII', PoolKind.NAMESPACE_SPEC, ni, li, ci, gi)

    if isinstance(val, FormalsSpec):
        idxs = (
            index_of(val.pos),
            index_of(val.pos_only),
            index_of(val.kwd),
            index_of(val.kwd_only),
            index_of(val.star),
            index_of(val.starstar),
            index_of(val.by_refs),
        )
        return struct.pack('<BIIIIIII', PoolKind.FORMALS_SPEC, *idxs)

    if isinstance(val, FunctionSpec):
        if pb is None:
            raise TypeError('FunctionSpec pool encoding requires a PoolBuilder')
        code = val.code
        instr = pb.soften(code)
        flg = code_flags(code)
        instr_b = encode_softened_instructions(pb, instr)
        ni = index_of(val.name)
        fi = index_of(val.formals_spec)
        nsi = index_of(code.namespace_spec)
        cni = index_of(code.name)
        cfi = index_of(code.filename)
        ic = len(instr)
        return struct.pack(
            '<BIIIIIII',
            PoolKind.FUNCTION_SPEC,
            ni,
            fi,
            nsi,
            cni,
            cfi,
            flg,
            ic) + instr_b

    if val is nil:
        return struct.pack('<B', PoolKind.NIL)

    if val is bracket_nil:
        return struct.pack('<B', PoolKind.BRACKET_NIL)

    if val is True:
        return struct.pack('<B', PoolKind.TRUE)

    if val is False:
        return struct.pack('<B', PoolKind.FALSE)

    if val is None:
        return struct.pack('<B', PoolKind.NONE)

    if val is Ellipsis:
        return struct.pack('<B', PoolKind.ELLIPSIS)

    if isinstance(val, int):
        return struct.pack('<B', PoolKind.INT) + _pack_int_big(val)

    if isinstance(val, float):
        return struct.pack('<Bd', PoolKind.FLOAT, val)

    if isinstance(val, complex):
        return struct.pack('<Bdd', PoolKind.COMPLEX, val.real, val.imag)

    if isinstance(val, str):
        return struct.pack('<B', PoolKind.STR) + _pack_string(val)

    if isinstance(val, bytes):
        return struct.pack('<B', PoolKind.BYTES) + struct.pack('<I', len(val)) + val

    if isinstance(val, symbol):
        # a gensym reaches a constant only in quote-syntax data or a
        # transformer template, which is source for a later expansion
        # that marks it afresh; sealed code reaches its bindings through
        # spelled operands, so the constant is written as its base and
        # no mark survives compilation
        return struct.pack('<B', PoolKind.SYMBOL) + _pack_string(
            str(val.base))

    if isinstance(val, keyword):
        return struct.pack('<B', PoolKind.KEYWORD) + _pack_string(str(val))

    if isinstance(val, (tuple, frozenset)):
        out = bytearray()
        out.append(
            PoolKind.TUPLE if isinstance(val, tuple) else PoolKind.FROZENSET)
        out.extend(struct.pack('<I', len(val)))
        for elem in val:
            out.extend(struct.pack('<I', index_of(elem)))
        return bytes(out)

    if isinstance(val, slice):
        return struct.pack(
            '<BIII', PoolKind.SLICE,
            index_of(val.start), index_of(val.stop), index_of(val.step))

    raise TypeError('unsupported pool value %r' % (val,))


def encode_pool(pool_values, index_of, pb=None):
    """
    Encode full pool section
    """

    chunks = [struct.pack('<I', len(pool_values))]
    for val in pool_values:
        chunks.append(encode_pool_record_value(val, index_of, pb))
    return b''.join(chunks)


def decode_pool_record_at_kind(kind, buf, off, prior):
    """
    Decode one pool record body after the kind byte into a final value

    ``prior`` is the list of decoded pool slots at indices ``0..len(prior)-1``
    (dependencies). Encoding uses ``intern_for_pool`` so those indices precede
    the current slot.

    :param kind: PoolKind value read from the stream
    :param buf: buffer
    :param off: offset after kind byte
    :param prior: decoded values for lower pool indices
    :return: pair of decoded value and new offset
    """

    if kind == PoolKind.NIL:
        return nil, off

    if kind == PoolKind.BRACKET_NIL:
        return bracket_nil, off

    if kind == PoolKind.TRUE:
        return True, off

    if kind == PoolKind.FALSE:
        return False, off

    if kind == PoolKind.ELLIPSIS:
        return Ellipsis, off

    if kind == PoolKind.NONE:
        return None, off

    if kind == PoolKind.INT:
        v, off = _unpack_int_big(buf, off)
        return v, off

    if kind == PoolKind.FLOAT:
        v = struct.unpack_from('<d', buf, off)[0]
        off += 8
        return v, off

    if kind == PoolKind.COMPLEX:
        r, i = struct.unpack_from('<dd', buf, off)
        off += 16
        return complex(r, i), off

    if kind == PoolKind.STR:
        s, off = _unpack_string(buf, off)
        return s, off

    if kind == PoolKind.BYTES:
        ln = struct.unpack_from('<I', buf, off)[0]
        off += 4
        b = buf[off:off + ln]
        off += ln
        return b, off

    if kind == PoolKind.SYMBOL:
        s, off = _unpack_string(buf, off)
        return symbol(s), off

    if kind == PoolKind.KEYWORD:
        s, off = _unpack_string(buf, off)
        return keyword(s), off

    if kind in (PoolKind.TUPLE, PoolKind.FROZENSET):
        arity = struct.unpack_from('<I', buf, off)[0]
        off += 4
        idxs = struct.unpack_from('<' + 'I' * arity, buf, off)
        off += 4 * arity
        build = tuple if kind == PoolKind.TUPLE else frozenset
        return build(prior[j] for j in idxs), off

    if kind == PoolKind.SLICE:
        si, ei, ti = struct.unpack_from('<III', buf, off)
        off += 12
        return slice(prior[si], prior[ei], prior[ti]), off

    if kind == PoolKind.NAMESPACE_SPEC:
        ni, li, ci, gi = struct.unpack_from('<IIII', buf, off)
        off += 16
        return NamespaceSpec(prior[ni], prior[li], prior[ci], prior[gi]), off

    if kind == PoolKind.FORMALS_SPEC:
        idxs = struct.unpack_from('<IIIIIII', buf, off)
        off += 28
        return FormalsSpec(*[prior[j] for j in idxs]), off

    if kind == PoolKind.FUNCTION_SPEC:
        ni, fi, nsi, cni, cfi, flg, ic = struct.unpack_from(
            '<IIIIIII', buf, off)
        off += 28
        ib = buf[off:off + ic * INSTRUCTION_SIZE]
        off += ic * INSTRUCTION_SIZE
        name = prior[ni]
        formals = prior[fi]
        nspec = prior[nsi]
        code_name = prior[cni]
        code_fn = prior[cfi]
        instr = []
        ioff = 0
        for _ in range(ic):
            (op, arg), ioff = decode_instruction(ib, ioff, prior)
            instr.append((op, arg))
        code = code_from_record(nspec, instr, code_name, code_fn, flg)
        return FunctionSpec(name, formals, code), off

    raise ValueError('unknown pool kind %s' % kind)


def decode_pool_records(buf, off):
    """
    Decode full pool section

    Slots are decoded in order; each record may use ``prior`` slots only
    (see ``intern_for_pool`` / ``decode_pool_record_at_kind``).
    """

    count = struct.unpack_from('<I', buf, off)[0]
    off += 4
    out = []
    for _ in range(count):
        kind = buf[off]
        off += 1
        val, off = decode_pool_record_at_kind(kind, buf, off, out)
        out.append(val)

    return out, off


# The end.
