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
koskari.compile.specials.syntax

Special forms for ``compile/syntax`` and
``compile/define-syntax``: Syntax box construction and macro registration.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
"""


from ...exceptions import CompilerError, SpecialError
from ...lib import OpCode, car, cdr, cons, nil, pair, symbol
from ...matcher import DEFINE, MATCH, VALUE, match
from ...types import get_position, is_pair, is_proper
from ..compiler import Compiler
from ..helpers import special, special_forms
from ...syntax import build_macro_rules_transformer, build_syntax_rules_transformer


_DEFINE_SYNTAX_PATTERNS = [MATCH(DEFINE("name"), VALUE("transformer"))]


@special(symbol("compile/make-syntax"))
def special_compile_syntax(
        compiler: Compiler,
        source: pair,
        tc: bool = False):
    """
    Compile (compile/make-syntax EXPR). Evaluates EXPR and wraps the result in
    ``Syntax`` with position from the expression form when available.
    """

    called_by, rest = source
    if not is_proper(rest) or cdr(rest) is not nil:
        raise SpecialError("make-syntax requires exactly one expression", source)

    inner = car(rest)
    pos = get_position(inner) if is_pair(inner) else get_position(source)

    return cons(symbol("syntax"), inner, pos, nil)


@special_forms(_DEFINE_SYNTAX_PATTERNS)
@special(symbol("compile/define-syntax"))
def special_define_syntax(compiler, source, tc=False):
    """
    Compile (define-syntax NAME (syntax-rules ...) or (macro-rules ...)).
    Registers a matcher/transformer object under the macro name.
    """

    called_by, rest = source
    matched = match(rest, _DEFINE_SYNTAX_PATTERNS)
    if matched is None:
        raise SpecialError("define-syntax requires a name and transformer", source)

    namesym = matched["name"].source
    transformer_spec = matched["transformer"].source

    try:
        if is_pair(transformer_spec) and car(transformer_spec) == symbol(
                "macro-rules"):
            transformer = build_macro_rules_transformer(
                compiler, namesym, transformer_spec)
            builder = symbol("build-macro-rules")
        else:
            transformer = build_syntax_rules_transformer(
                namesym, transformer_spec)
            builder = symbol("build-syntax-rules")
    except CompilerError as ex:
        raise SpecialError(str(ex), source) from ex

    tagged = transformer.to_tuple()
    build_call = cons(builder, str(namesym), tagged, nil)

    frag = compiler.fragment(called_by)
    pos = matched["name"].source_pos
    frag.declare_var(str(namesym), position=pos)

    frag.merge(compiler.full_compile(build_call))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, str(namesym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
