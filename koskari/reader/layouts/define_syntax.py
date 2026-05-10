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

Layout handlers for define-syntax and transformer block syntax (syntax-rules,
macro-rules, special-rules, syntax-match, macro-match, special-match).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...exceptions import LayoutError
from ...lib import car, cons, nil, symbol
from ...matcher import (DEFINE, LIST, LITERAL_SYM, MATCH, PAIR, REPEAT, VALUE,
                        match, match1)
from ...stdlib import builtin
from ...types import (
    get_position,
    is_keyword,
    is_proper,
    is_symbol,
    unpack,
)
from .helpers import layout


_ALLOWED_DEFINE_SYNTAX_TRANSFORMERS = frozenset({
    'syntax-rules',
    'macro-rules',
    'special-rules',
    'syntax-match',
    'macro-match',
    'special-match',
})


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

_MACRO_RULES_FORM = MATCH(
    LIST(LITERAL_SYM('macro-rules'), VALUE('literals_expr')),
    REPEAT('rules', PAIR('rule'), min_count=1))

_SPECIAL_RULES_FORM = MATCH(
    LIST(LITERAL_SYM('special-rules'), VALUE('literals_expr')),
    REPEAT('rules', PAIR('rule'), min_count=1))

_SYNTAX_MATCH_FORM = MATCH(
    LIST(LITERAL_SYM('syntax-match')),
    REPEAT('rules', PAIR('rule'), min_count=1))

_MACRO_MATCH_FORM = MATCH(
    LIST(LITERAL_SYM('macro-match')),
    REPEAT('rules', PAIR('rule'), min_count=1))

_SPECIAL_MATCH_FORM = MATCH(
    LIST(LITERAL_SYM('special-match')),
    REPEAT('rules', PAIR('rule'), min_count=1))


_symbol_syntax_rules = symbol('syntax-rules')
_symbol_macro_rules = symbol('macro-rules')
_symbol_special_rules = symbol('special-rules')
_symbol_syntax_match = symbol('syntax-match')
_symbol_macro_match = symbol('macro-match')
_symbol_special_match = symbol('special-match')
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


def _pattern_template_from_rule(rule_source):
    """
    Split a syntax-rules-shaped rule block into pattern and template datums.

    :param rule_source: pair source for one rule block
    :return: ``(pattern, template)``
    """

    rule_parts = list(unpack(rule_source))
    if len(rule_parts) < 2:
        raise LayoutError(
            'rule block must have pattern and template',
            get_position(rule_source))
    pattern = cons(*rule_parts[:-1], nil)
    template = rule_parts[-1]
    match_m = match1(pattern, [_MATCH_FORM_PATTERN])
    if match_m is not None:
        pattern = match_m['inner'].source
    return pattern, template


def _syntax_rules_like_rows(rule_matches):
    """
    Build (PATTERN TEMPLATE) rows for syntax-rules / macro-rules / special-rules.

    :param rule_matches: ``repeat.matches`` from a syntax-rules-shaped rule list
    :return: list of ``cons(pattern, template, nil)`` forms
    """

    rules = []
    for sub_res, _ in rule_matches:
        pattern, template = _pattern_template_from_rule(sub_res['rule'].source)
        rules.append(cons(pattern, template, nil))
    return rules


@builtin('layout/syntax-rules')
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
    rules = _syntax_rules_like_rows(m['rules'].matches)

    return cons(_symbol_syntax_rules, literals, *rules, nil)


@builtin('layout/macro-rules')
@layout(symbol('layout/macro-rules'))
def layout_macro_rules(source, peers):
    """
    Lower macro-rules block to (macro-rules (LIT ...) ((PAT) TMPL) ...).

    Same block shape as syntax-rules; only the emitted head differs.
    """

    if peers:
        raise LayoutError(
            'macro-rules block does not take peers',
            get_position(source))

    m = match(source, _MACRO_RULES_FORM)
    if m is None:
        raise LayoutError(
            'macro-rules requires literals list and at least one rule',
            get_position(source))

    literals = _literals_to_list(m['literals_expr'].source, source)
    rules = _syntax_rules_like_rows(m['rules'].matches)

    return cons(_symbol_macro_rules, literals, *rules, nil)


@builtin('layout/special-rules')
@layout(symbol('layout/special-rules'))
def layout_special_rules(source, peers):
    """
    Lower special-rules block to (special-rules (LIT ...) ((PAT) TMPL) ...).

    Same block shape as macro-rules; only the emitted head differs.
    """

    if peers:
        raise LayoutError(
            'special-rules block does not take peers',
            get_position(source))

    m = match(source, _SPECIAL_RULES_FORM)
    if m is None:
        raise LayoutError(
            'special-rules requires literals list and at least one rule',
            get_position(source))

    literals = _literals_to_list(m['literals_expr'].source, source)
    rules = _syntax_rules_like_rows(m['rules'].matches)

    return cons(_symbol_special_rules, literals, *rules, nil)


@builtin('layout/syntax-match')
@layout(symbol('layout/syntax-match'))
def layout_syntax_match(source, peers):
    """
    Lower syntax-match block to (syntax-match (PAT TMPL) ...).

    No literals list; each rule is (pattern . (template . nil)).
    """

    if peers:
        raise LayoutError(
            'syntax-match block does not take peers',
            get_position(source))

    m = match(source, _SYNTAX_MATCH_FORM)
    if m is None:
        raise LayoutError(
            'syntax-match requires at least one rule',
            get_position(source))

    rules = []
    for sub_res, _ in m['rules'].matches:
        pattern, template = _pattern_template_from_rule(sub_res['rule'].source)
        rules.append(cons(pattern, template, nil))

    return cons(_symbol_syntax_match, *rules, nil)


@builtin('layout/macro-match')
@layout(symbol('layout/macro-match'))
def layout_macro_match(source, peers):
    """
    Lower macro-match block to (macro-match (PATTERN BODY ...) ...).

    Same rule shape as macro-rules blocks: pattern forms then template / body.
    """

    if peers:
        raise LayoutError(
            'macro-match block does not take peers',
            get_position(source))

    m = match(source, _MACRO_MATCH_FORM)
    if m is None:
        raise LayoutError(
            'macro-match requires at least one rule',
            get_position(source))

    rules = []
    for sub_res, _ in m['rules'].matches:
        pattern, template = _pattern_template_from_rule(sub_res['rule'].source)
        rules.append(cons(pattern, template, nil))

    return cons(_symbol_macro_match, *rules, nil)


@builtin('layout/special-match')
@layout(symbol('layout/special-match'))
def layout_special_match(source, peers):
    """
    Lower special-match block to (special-match (PATTERN BODY ...) ...).

    Same rule shape as macro-match blocks: pattern forms then template / body.
    """

    if peers:
        raise LayoutError(
            'special-match block does not take peers',
            get_position(source))

    m = match(source, _SPECIAL_MATCH_FORM)
    if m is None:
        raise LayoutError(
            'special-match requires at least one rule',
            get_position(source))

    rules = []
    for sub_res, _ in m['rules'].matches:
        pattern, template = _pattern_template_from_rule(sub_res['rule'].source)
        rules.append(cons(pattern, template, nil))

    return cons(_symbol_special_match, *rules, nil)


@builtin('layout/define-syntax')
@layout(symbol('layout/define-syntax'))
def layout_define_syntax(source, peers):
    """
    Lower define-syntax block to (compile/define-syntax NAME TRANSFORMER).

    Expects exactly one body form: syntax-rules, macro-rules, special-rules,
    syntax-match, macro-match, or special-match.
    """

    if peers:
        raise LayoutError(
            'define-syntax block does not take peers',
            get_position(source))

    m = match(source, _DEFINE_SYNTAX_FORMS)
    if m is None:
        raise LayoutError(
            'define-syntax block requires name and single transformer form',
            get_position(source))

    name = m['name'].source
    transformer_spec = m['transformer'].source
    head = car(transformer_spec)
    if not is_symbol(head) or str(head) not in _ALLOWED_DEFINE_SYNTAX_TRANSFORMERS:
        raise LayoutError(
            'define-syntax requires syntax-rules, macro-rules, special-rules, '
            'syntax-match, macro-match, or special-match',
            get_position(transformer_spec))

    return cons(_symbol_compile_define_syntax, name, transformer_spec, nil)


# The end.
