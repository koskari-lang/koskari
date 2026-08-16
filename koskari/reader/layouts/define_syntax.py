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


from ...compile.helpers import match1
from ...exceptions import LayoutError
from ...lib import cons, nil, symbol
from ...matcher import BIND, CALLEDBY, LIST, LITERAL, MATCH, SYMBOL
from ...bootstrap import builtin
from ...types import is_nil, get_position, unpack
from .helpers import block_source_from_cons, layout, match_block_source


_case_symbol = symbol('case')


_MATCH_FORM_PATTERN = LIST(LITERAL(_case_symbol), BIND('inner'))

_RULES_HEAD_PATTERNS = [
    MATCH(LIST(CALLEDBY(), SYMBOL("name"), BIND('literals_expr'))),
    MATCH(LIST(CALLEDBY(), BIND('literals_expr'))),
]

_MATCH_HEAD_PATTERNS = [
    MATCH(LIST(CALLEDBY(), SYMBOL("name"))),
    MATCH(LIST(CALLEDBY())),
]

_BARE_HEAD_PATTERNS = [
    MATCH(LIST(CALLEDBY(), SYMBOL("name"), SYMBOL("slot"))),
    MATCH(LIST(CALLEDBY(), SYMBOL("slot"))),
]


_symbol_begin = symbol('begin')
_symbol_bare_syntax = symbol('bare-syntax')
_symbol_syntax_rules = symbol('syntax-rules')
_symbol_macro_rules = symbol('macro-rules')
_symbol_special_rules = symbol('special-rules')
_symbol_syntax_match = symbol('syntax-match')
_symbol_macro_match = symbol('macro-match')
_symbol_special_match = symbol('special-match')


def _pattern_from_header(header):
    """
    Build a rule pattern from a block header, unwrapping ``case`` when present

    :param header: block header forms ``(head . args)``
    :return: pattern datum for one rule row
    """

    pattern = header
    match_m = match1(pattern, [_MATCH_FORM_PATTERN])
    if match_m is not None:
        pattern = match_m['inner'].datum
    return pattern


def _rule_row(ns, rule_source, layout_body=False):
    """
    Lower one raw rule block to ``(pattern . template-forms...)``

    :param ns: layout handler namespace used when layout_body is true
    :param rule_source: pass-one block source for one rule
    :param layout_body: expand nested :block: forms in the template body
    :return: one rule row cons
    """

    rule_source = block_source_from_cons(rule_source) or rule_source
    matched = match_block_source(rule_source)
    if matched is None:
        raise LayoutError(
            'rule must be a block',
            get_position(rule_source))

    header = matched['header'].datum
    body = matched['body'].datum

    if is_nil(body):
        raise LayoutError(
            'rule block must have pattern and template',
            get_position(rule_source))

    pattern = _pattern_from_header(header)
    if layout_body:
        from . import apply_layouts
        body = apply_layouts(body, ns)
    template = cons(_symbol_begin, body)
    return cons(pattern, template, nil)


def _rule_rows(ns, parent_body, layout_body=False):
    """
    Lower each raw rule block in a transformer body

    :param ns: layout handler namespace used when layout_body is true
    :param parent_body: body spine following the transformer header
    :param layout_body: expand nested :block: forms in each rule body
    :return: list of rule row cons forms
    """

    rules = []
    for rule_source in unpack(parent_body):
        rules.append(_rule_row(ns, rule_source, layout_body))
    return rules


def _lower_rules(ns, source, peers, head_sym, head_patterns,
                 with_literals, layout_body=False):
    """
    Lower a transformer block with raw rule children

    :param ns: layout handler namespace
    :param source: pass-one block source for the transformer block
    :param peers: peer block sources, must be empty
    :param head_sym: emitted form head symbol
    :param head_patterns: matcher patterns for the block header
    :param with_literals: whether the header includes a literals list
    :param layout_body: expand nested :block: forms in each rule body
    :return: lowered transformer form
    """

    if peers:
        raise LayoutError(
            '%s block does not take peers' % (head_sym,),
            get_position(source))

    parent_header, parent_body = source

    head_m = match1(parent_header, head_patterns)
    if head_m is None:
        if with_literals:
            raise LayoutError(
                '%s requires literals list and at least one rule' % (
                    head_sym,),
                get_position(source))
        raise LayoutError(
            '%s requires at least one rule' % (head_sym,),
            get_position(source))

    rules = _rule_rows(ns, parent_body, layout_body)

    if not rules:
        raise LayoutError(
            '%s requires at least one rule' % (head_sym,),
            get_position(source))

    ret = cons(*rules, nil)

    if with_literals:
        literals = head_m['literals_expr'].datum
        ret = cons(literals, ret)

    if 'name' in head_m:
        ret = cons(head_m['name'].datum, ret)

    return cons(head_sym, ret)


@builtin('layout/syntax-rules')
@layout(symbol('layout/syntax-rules'), raw=True)
def layout_syntax_rules(ns, source, peers):
    """
    Lower syntax-rules block to ``(syntax-rules (LIT ...) (PATTERN . TMPL...) ...)``.

    Each raw rule child supplies the pattern column from its header and
    template forms from its body.
    """

    return _lower_rules(
        ns, source, peers, _symbol_syntax_rules,
        _RULES_HEAD_PATTERNS, True)


@builtin('layout/bare-syntax')
@layout(symbol('layout/bare-syntax'), raw=True)
def layout_bare_syntax(ns, source, peers):
    """
    Lower bare-syntax block to ``(bare-syntax SLOT (begin TMPL...))``, or
    with a leading name in the header, ``(bare-syntax NAME SLOT (begin
    TMPL...))``.

    The header names the called-by slot; the body is the template, left
    unexpanded like a syntax-rules template.
    """

    if peers:
        raise LayoutError(
            'bare-syntax block does not take peers',
            get_position(source))

    header, body = source

    head_m = match1(header, _BARE_HEAD_PATTERNS)
    if head_m is None:
        raise LayoutError(
            'bare-syntax requires a called-by symbol',
            get_position(source))

    if is_nil(body):
        raise LayoutError(
            'bare-syntax requires a template',
            get_position(source))

    ret = cons(cons(_symbol_begin, body), nil)
    ret = cons(head_m['slot'].datum, ret)

    if 'name' in head_m:
        ret = cons(head_m['name'].datum, ret)

    return cons(_symbol_bare_syntax, ret)


@builtin('layout/macro-rules')
@layout(symbol('layout/macro-rules'), raw=True)
def layout_macro_rules(ns, source, peers):
    """
    Lower macro-rules block to ``(macro-rules (LIT ...) (PATTERN . BODY...) ...)``.

    Same block shape as syntax-rules; only the emitted head differs.
    """

    return _lower_rules(
        ns, source, peers, _symbol_macro_rules,
        _RULES_HEAD_PATTERNS, True, layout_body=True)


@builtin('layout/special-rules')
@layout(symbol('layout/special-rules'), raw=True)
def layout_special_rules(ns, source, peers):
    """
    Lower special-rules block to ``(special-rules (LIT ...) (PATTERN . BODY...) ...)``.

    Same block shape as macro-rules; only the emitted head differs.
    """

    return _lower_rules(
        ns, source, peers, _symbol_special_rules,
        _RULES_HEAD_PATTERNS, True, layout_body=True)


@builtin('layout/syntax-match')
@layout(symbol('layout/syntax-match'), raw=True)
def layout_syntax_match(ns, source, peers):
    """
    Lower syntax-match block to ``(syntax-match (PATTERN . TMPL...) ...)``.

    No literals list; each raw rule child supplies pattern and template forms.
    """

    return _lower_rules(
        ns, source, peers, _symbol_syntax_match,
        _MATCH_HEAD_PATTERNS, False)


@builtin('layout/macro-match')
@layout(symbol('layout/macro-match'), raw=True)
def layout_macro_match(ns, source, peers):
    """
    Lower macro-match block to ``(macro-match (PATTERN . BODY...) ...)``.

    Same rule shape as macro-rules blocks.
    """

    return _lower_rules(
        ns, source, peers, _symbol_macro_match,
        _MATCH_HEAD_PATTERNS, False, layout_body=True)


@builtin('layout/special-match')
@layout(symbol('layout/special-match'), raw=True)
def layout_special_match(ns, source, peers):
    """
    Lower special-match block to ``(special-match (PATTERN . BODY...) ...)``.

    Same rule shape as macro-match blocks.
    """

    return _lower_rules(
        ns, source, peers, _symbol_special_match,
        _MATCH_HEAD_PATTERNS, False, layout_body=True)


# The end.
