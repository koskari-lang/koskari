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
"""


from ...syntax import special
from .begin import special_begin
from .bytecode import special_bytecode
from .call import special_call, special_call_attr
from .class_ import special_class
from .collections import special_build_list, special_list_append
from .conditionals import special_and, special_cond, special_or, special_switch
from .context import special_dyn_with, special_parameterize, special_with
from .continuations import (
    special_await,
    special_call_cc,
    special_capture_cc,
    special_let_cc,
    special_reset,
    special_return,
    special_shift,
)
from .dyn_wind import special_dyn_wind
from .exceptions import (
    special_check_exception,
    special_clear_exception,
    special_load_exception,
    special_raise,
    special_try,
)
from .fence import special_apply_fence
from .imports import (special_import, special_import_all_from,
                      special_import_from)
from .items import special_del_item, special_item, special_set_item, special_take_item
from .lambda_ import special_lambda
from .let import special_let
from .loops import special_break, special_continue, special_for_each, special_while
from .object import (
    special_add,
    special_aiter,
    special_bit_and,
    special_bit_invert,
    special_bit_left,
    special_bit_or,
    special_bit_right,
    special_bit_xor,
    special_dir,
    special_divmod,
    special_eq,
    special_floor_divide,
    special_format,
    special_ge,
    special_gt,
    special_in,
    special_inplace,
    special_is,
    special_is_not,
    special_isinstance,
    special_issubclass,
    special_iter,
    special_le,
    special_len,
    special_lt,
    special_matrix_multiply,
    special_modulo,
    special_multiply,
    special_ne,
    special_not,
    special_not_in,
    special_power,
    special_repr,
    special_str,
    special_subtract,
    special_true_divide,
    special_truth,
    special_type,
    special_vars,
)
from .pairs import special_car, special_cdr, special_cons, special_quote
from .quasiquote import (
    special_quasiquote,
    special_unquote,
    special_unquote_splicing,
)
from .refs import (
    special_bindq,
    special_deref,
    special_ref_attr,
    special_ref_item,
    special_refq,
)
from .slots import special_attr, special_del_attr, special_set_attr, special_take_attr
from .syntax import (
    special_compile_macro_match,
    special_compile_macro_rules,
    special_compile_special_match,
    special_compile_special_rules,
    special_compile_syntax_match,
    special_compile_syntax_rules,
)
from .thunk import special_gensym_thunk
from .variables import (
    special_define,
    special_define_global,
    special_define_values,
    special_delq,
    special_globals,
    special_locals,
    special_setq,
    special_setq_values,
    special_takeq,
)


__all__ = (
    "special",
    "special_add",
    "special_and",
    "special_apply_fence",
    "special_apply_layout",
    "special_aiter",
    "special_await",
    "special_attr",
    "special_begin",
    "special_bit_and",
    "special_bit_invert",
    "special_bit_left",
    "special_bit_or",
    "special_bit_right",
    "special_bit_xor",
    "special_build_list",
    "special_import",
    "special_import_all_from",
    "special_import_from",
    "special_bindq",
    "special_break",
    "special_bytecode",
    "special_call",
    "special_call_attr",
    "special_class",
    "special_call_cc",
    "special_compile_macro_match",
    "special_compile_macro_rules",
    "special_compile_special_match",
    "special_compile_special_rules",
    "special_compile_syntax",
    "special_compile_syntax_match",
    "special_compile_syntax_rules",
    "special_capture_cc",
    "special_car",
    "special_cdr",
    "special_cond",
    "special_cons",
    "special_continue",
    "special_del_attr",
    "special_deref",
    "special_del_item",
    "special_define",
    "special_define_global",
    "special_define_values",
    "special_delq",
    "special_dir",
    "special_divmod",
    "special_eq",
    "special_check_exception",
    "special_clear_exception",
    "special_dyn_wind",
    "special_dyn_with",
    "special_floor_divide",
    "special_format",
    "special_for_each",
    "special_ge",
    "special_gensym_thunk",
    "special_globals",
    "special_gt",
    "special_in",
    "special_inplace",
    "special_is",
    "special_is_not",
    "special_isinstance",
    "special_issubclass",
    "special_iter",
    "special_item",
    "special_lambda",
    "special_le",
    "special_len",
    "special_let",
    "special_list_append",
    "special_load_exception",
    "special_let_cc",
    "special_locals",
    "special_lt",
    "special_matrix_multiply",
    "special_modulo",
    "special_multiply",
    "special_ne",
    "special_not",
    "special_not_in",
    "special_or",
    "special_parameterize",
    "special_power",
    "special_quasiquote",
    "special_quote",
    "special_raise",
    "special_ref_attr",
    "special_ref_item",
    "special_refq",
    "special_repr",
    "special_reset",
    "special_return",
    "special_set_attr",
    "special_set_item",
    "special_setq",
    "special_setq_values",
    "special_shift",
    "special_str",
    "special_switch",
    "special_subtract",
    "special_true_divide",
    "special_truth",
    "special_take_attr",
    "special_take_item",
    "special_takeq",
    "special_try",
    "special_type",
    "special_unquote",
    "special_unquote_splicing",
    "special_vars",
    "special_while",
    "special_with",
)


# The end.
