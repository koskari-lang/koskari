# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will to be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
koskari.reader.layouts.define_syntax

Layout handlers for define-syntax and syntax-rules block syntax.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...exceptions import LayoutError
from ...types import cons, get_position, is_pair, is_proper, is_symbol, nil, symbol, unpack
from .helpers import layout


_symbol_syntax_rules = symbol('syntax-rules')
_symbol_compile_begin = symbol('compile/begin')
_symbol_compile_define_syntax = symbol('compile/define-syntax')


def _literals_to_list(literals_expr, source):
    """
    Convert a literals expression to a proper list of symbols.

    Accepts () nil, (x y), [x y], etc. Raises LayoutError if invalid.
    """
    if literals_expr is nil:
        return nil
    if not is_proper(literals_expr):
        raise LayoutError(
            'syntax-rules literals must be a proper list',
            get_position(literals_expr))
    literals = list(unpack(literals_expr))
    for lit in literals:
        if not is_symbol(lit):
            raise LayoutError(
                'syntax-rules literals must be symbols',
                get_position(literals_expr))
    return cons(*literals, nil)


@layout(symbol('layout/syntax-rules'))
def layout_syntax_rules(source, peers):
    """
    Lower syntax-rules block to (syntax-rules (LIT ...) ((PAT) TMPL) ...).

    Each rule block becomes ((pattern) template) where pattern is the block
    header and template is the block body wrapped in compile/begin.
    """

    if peers:
        raise LayoutError(
            'syntax-rules block does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))

    if len(header_parts) < 2:
        raise LayoutError(
            'syntax-rules requires a literals list',
            get_position(source))

    if str(header_parts[0]) != 'syntax-rules':
        raise LayoutError(
            'expected syntax-rules block',
            get_position(source))

    literals_expr = header_parts[1]
    if len(header_parts) > 2:
        raise LayoutError(
            'syntax-rules takes only one literals argument',
            get_position(source))

    literals = _literals_to_list(literals_expr, source)
    rule_forms = list(unpack(body))

    if not rule_forms:
        raise LayoutError(
            'syntax-rules requires at least one rule',
            get_position(source))

    rules = []
    for raw_rule in rule_forms:
        if not is_pair(raw_rule):
            raise LayoutError(
                'invalid syntax-rules rule',
                get_position(raw_rule))
        parts = list(unpack(raw_rule))
        if len(parts) < 2:
            raise LayoutError(
                'syntax-rules rule must have pattern and template',
                get_position(raw_rule))
        pattern = cons(*parts[:-1], nil)
        template = parts[-1]
        rules.append(cons(pattern, cons(template, nil)))

    return cons(
        _symbol_syntax_rules,
        cons(literals, cons(*rules, nil)))


@layout(symbol('layout/define-syntax'))
def layout_define_syntax(source, peers):
    """
    Lower define-syntax block to (compile/define-syntax NAME (syntax-rules ...)).

    Expects exactly one body form: a syntax-rules form.
    """

    if peers:
        raise LayoutError(
            'define-syntax block does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))

    if len(header_parts) != 2:
        raise LayoutError(
            'define-syntax requires a name',
            get_position(source))

    if str(header_parts[0]) not in ('define-syntax', 'compile/define-syntax'):
        raise LayoutError(
            'expected define-syntax block',
            get_position(source))

    name = header_parts[1]
    if not is_symbol(name):
        raise LayoutError(
            'define-syntax name must be a symbol',
            get_position(source))

    body_forms = list(unpack(body))
    if len(body_forms) != 1:
        raise LayoutError(
            'define-syntax requires exactly one syntax-rules form',
            get_position(source))

    transformer_spec = body_forms[0]
    if not is_pair(transformer_spec):
        raise LayoutError(
            'define-syntax body must be a syntax-rules form',
            get_position(transformer_spec))

    head = transformer_spec[0]
    if not is_symbol(head) or str(head) != 'syntax-rules':
        raise LayoutError(
            'define-syntax requires a syntax-rules form',
            get_position(transformer_spec))

    return cons(
        _symbol_compile_define_syntax,
        cons(name, cons(transformer_spec, nil)))


# The end.
