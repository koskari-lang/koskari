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
koskari.compile.specials

Special form handlers for the Koskari compiler.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ..helpers import SPECIALS, special

from .begin import special_begin
from .bytecode import special_bytecode
from .call import special_call
from .conditionals import special_cond, special_if, special_unless, special_when
from .continuations import special_call_cc, special_let_cc, special_capture_cc, special_reset, special_shift
from .context import special_dyn_with, special_parameterize, special_with
from .exceptions import (
    special_check_exception,
    special_clear_exception,
    special_load_exception,
    special_raise,
    special_try,
)
from .items import special_del_item, special_item, special_ref_item, special_set_item
from .lambda_ import special_lambda
from .let import special_let
from .loops import special_break, special_continue, special_for_each, special_while
from .pairs import special_car, special_cdr, special_cons, special_quote
from .slots import special_attr, special_del_attr, special_ref_attr, special_set_attr
from .imports import helper_import_from_sequence, special_import, special_import_from
from .variables import special_bindq, special_define, special_delq, special_refq, special_setq


__all__ = (
    "SPECIALS",
    "special",
    "special_attr",
    "special_begin",
    "helper_import_from_sequence",
    "special_import",
    "special_import_from",
    "special_bindq",
    "special_break",
    "special_bytecode",
    "special_call",
    "special_call_cc",
    "special_capture_cc",
    "special_car",
    "special_cdr",
    "special_cond",
    "special_cons",
    "special_continue",
    "special_del_attr",
    "special_del_item",
    "special_define",
    "special_delq",
    "special_check_exception",
    "special_clear_exception",
    "special_dyn_with",
    "special_for_each",
    "special_if",
    "special_item",
    "special_lambda",
    "special_let",
    "special_load_exception",
    "special_let_cc",
    "special_parameterize",
    "special_quote",
    "special_raise",
    "special_ref_attr",
    "special_ref_item",
    "special_refq",
    "special_reset",
    "special_set_attr",
    "special_set_item",
    "special_setq",
    "special_shift",
    "special_try",
    "special_unless",
    "special_when",
    "special_while",
    "special_with",
)


# The end.
