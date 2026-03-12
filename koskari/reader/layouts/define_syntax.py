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
from ...matcher import (DEFINE, LIST, LITERAL_SYM, MATCH, PAIR, REPEAT, VALUE,
                        match, match1)
from ...types import (car, cons, get_position, is_keyword, is_proper,
                      is_symbol, nil, symbol, unpack)
from .helpers import layout


_DEFINE_SYNTAX_FORMS = [
    MATCH(
        LIST(LITERAL_SYM('define-syntax'), DEFINE('name')),
        PAIR('transformer')),
    MATCH(
        LIST(LITERAL_SYM('compile/define-syntax'), DEFINE('name')),
        PAIR('transformer')),
]

_MATCH_FORM_PATTERN = LIST(LITERAL_SYM('match'), VALUE('inner'))

_SYNTAX_RULES_FORM = MATCH(
    LIST(LITERAL_SYM('syntax-rules'), VALUE('literals_expr')),
    REPEAT('rules', PAIR('rule'), min_count=1))


_symbol_syntax_rules = symbol('syntax-rules')
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
        if not (is_symbol(lit) or is_keyword(lit)):
            raise LayoutError(
                'syntax-rules literals must be symbols or keywords',
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

    m = match(source, _SYNTAX_RULES_FORM)
    if m is None:
        raise LayoutError(
            'syntax-rules requires literals list and at least one rule',
            get_position(source))

    literals = _literals_to_list(m['literals_expr'].source, source)
    rules = []
    for sub_res, _ in m['rules'].matches:
        rule_parts = list(unpack(sub_res['rule'].source))
        if len(rule_parts) < 2:
            raise LayoutError(
                'syntax-rules rule must have pattern and template',
                get_position(sub_res['rule'].source))
        pattern = cons(*rule_parts[:-1], nil)
        template = rule_parts[-1]
        match_m = match1(pattern, [_MATCH_FORM_PATTERN])
        if match_m is not None:
            pattern = match_m['inner'].source
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

    m = match(source, _DEFINE_SYNTAX_FORMS)
    if m is None:
        raise LayoutError(
            'define-syntax block requires name and single syntax-rules form',
            get_position(source))

    name = m['name'].source
    transformer_spec = m['transformer'].source
    if not is_symbol(car(transformer_spec)) or str(car(transformer_spec)) != 'syntax-rules':
        raise LayoutError(
            'define-syntax requires a syntax-rules form',
            get_position(transformer_spec))

    return cons(
        _symbol_compile_define_syntax,
        cons(name, cons(transformer_spec, nil)))


# The end.
