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
# MINOR bumped: MATCH_CASE's third EXTRA slot becomes the missing-externs
# name tuple, whose values the compiler pushes above the subject; the
# opcode no longer resolves externs from the namespace itself.
# MINOR bumped: IMPORT_USE opcode appended for the use form's
# feature-family import.
# MINOR bumped: fused format opcodes VAR_FORMAT / VAR_FORMAT_CONST and
# FAST_FORMAT / FAST_FORMAT_CONST inserted mid-enum, renumbering every
# opcode after the VAR and FAST families.
# MINOR bumped: TAIL_CALL twins for the ATTR, VAR, and FAST call
# families inserted after TAIL_CALL_STAR, renumbering every opcode
# after the tail-call family.
# MINOR bumped: CONST2 bulk constant load inserted after CONST,
# renumbering every opcode after it.
# MINOR bumped: MATCH_CASE binds its captures into the namespace on a
# match and pushes nothing; MATCH_STORE_NAMES removed, renumbering
# every opcode after it.
# PATCH bumped: .kio pool kinds ELLIPSIS, FROZENSET, SLICE added; code
# body and function-spec flags carry protect (0x02) and cache (0x04).
# MINOR bumped: VAR_ITEM, VAR_CONST_ITEM, VAR_ITEM_STORE,
# VAR_CONST_ITEM_STORE, VAR_ITEM_STORE_CONST,
# VAR_CONST_ITEM_STORE_CONST, the four VAR item in-place rows, and
# their FAST twins inserted after the attr rows of the VAR and FAST
# families, and ITEM_INPLACE and CONST_ITEM_INPLACE appended to the
# stack item tiers, renumbering every opcode after them; ITEM,
# CONST_ITEM, ITEM_STORE, CONST_ITEM_STORE gain an elector cache slot,
# and later ITEM_INPLACE and CONST_ITEM_INPLACE too (slots are not
# serialized; no bump).
# MINOR bumped: RETURN_CONST and YIELD_CONST inserted after YIELD in
# mode control, RETURN_VAR and YIELD_VAR after VAR_FORMAT_CONST, and
# RETURN_FAST and YIELD_FAST after FAST_FORMAT_CONST, renumbering every
# opcode after them.
# MINOR bumped: the item family renamed to the three-position grammar
# (object marker, ITEM, key marker, verb, value marker) and filled out
# to every combination of stack, constant, and named object, key, and
# value for ITEM, CLEAR, TAKE, REF, STORE, and INPLACE, renumbering
# every opcode after the VAR family. The FAST rows and the stack rows
# of ITEM, STORE, and INPLACE carry a KOS_CACHE_ITEM slot (slots are
# not serialized).
# PATCH bumped: .kio pool kind GENSYM added, a template symbol with the
# expansion marks it carries.
# PATCH bumped: pool kind GENSYM removed; a gensym is written as the
# SYMBOL record of its scoped spelling.
# PATCH bumped: a frozen matcher's CAPTURE row is (kind, slot, subnode);
# the unread hygiene_role and scope_slot elements are gone.
# PATCH bumped: a gensym constant is written as the SYMBOL record of its
# base.
# MINOR bumped: BRACKET_CONS and BRACKET_PROPER inserted after PROPER,
# renumbering every opcode after them.
# MINOR bumped: VAR_INPLACE and FAST_INPLACE rows carry a bare OPERAND
# selector in a0 instead of an OPCOUNT whose count was always 1.
# MINOR bumped: OBJECT_OPERAND_VAR, OBJECT_OPERAND_FAST, VAR_OPERAND_VAR
# and FAST_OPERAND_FAST inserted after FAST_OPERAND_CONST, renumbering
# THEEND.
# MINOR bumped: BRACKET_SPLICE inserted after PAIR_SPLICE, renumbering
# every opcode after it.
BYTECODE_ABI_MAJOR = 0
BYTECODE_ABI_MINOR = 23
BYTECODE_ABI_MICRO = 0
BYTECODE_ABI_PATCH = 0


# The end.
