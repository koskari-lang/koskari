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


from ...bootstrap import builtin
from ...exceptions import LayoutError
from ...lib import cons, demand, nil, symbol
from ...matcher import (
    BIND, CALLEDBY, LIST, LITERAL, MATCH, SYMBOL, first_match, make_matcher)
from ...types import get_position, is_nil, is_pair, unpack
from .helpers import (
    block_source_from_cons, is_block, layout, match_block_source)


_case_symbol = symbol('case')


# each matcher built once and run over a one-cell spine of the form;
# a header tries its head matchers in order through first_match
_match_form = demand(make_matcher(LIST(LITERAL(_case_symbol), BIND('inner'))))

_RULES_HEAD_MATCHERS = (
    make_matcher(MATCH(
        LIST(CALLEDBY(), SYMBOL("name"), BIND('literals_expr')))),
    make_matcher(MATCH(LIST(CALLEDBY(), BIND('literals_expr')))),
)

_MATCH_HEAD_MATCHERS = (
    make_matcher(MATCH(LIST(CALLEDBY(), SYMBOL("name")))),
    make_matcher(MATCH(LIST(CALLEDBY()))),
)

_BARE_HEAD_MATCHERS = (
    make_matcher(MATCH(LIST(CALLEDBY(), SYMBOL("name"), SYMBOL("slot")))),
    make_matcher(MATCH(LIST(CALLEDBY(), SYMBOL("slot")))),
)


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
    match_m = _match_form(cons(pattern, nil))
    if match_m is not None:
        pattern = match_m['inner'].datum
    return pattern


def _rule_row(reader, rule_source, layout_body=False):
    """
    Lower one raw rule block to ``(pattern . template-forms...)``

    :param reader: the LayoutReader expanding the source
    :param rule_source: one rule's :#block: entry
    :param layout_body: expand nested :#block: forms in a parenthesized
        rule's template; a block rule's body is always expanded
    :return: one rule row cons
    """

    from . import apply_layouts, apply_lines

    # a rule is a case block, whose template body is the block's
    # entries, or a parenthesized (PATTERN TEMPLATE ...) form on a line,
    # whose template body is already forms
    as_block = is_block(rule_source)
    if not as_block:
        rule_source = reader.fold_line(rule_source)
    source = block_source_from_cons(rule_source) or rule_source
    matched = match_block_source(source)
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
    if as_block:
        # lower the nested blocks in place first so a block gathers its
        # declared peers from the entries after it, as the default block
        # handler does for a non-raw body
        body = reader.fold_lines(apply_lines(body, reader))
    elif layout_body:
        body = apply_layouts(body, reader)
    if not is_nil(body) and is_pair(body) and is_nil(body.cdr):
        # a single-form body is its own template; the begin wrapper is
        # only for sequencing
        template = body.car
    else:
        template = cons(_symbol_begin, body)
    return cons(pattern, template, nil)


def _rule_rows(reader, parent_body, layout_body=False):
    """
    Lower each raw rule block in a transformer body

    :param reader: the LayoutReader expanding the source
    :param parent_body: the entries following the transformer header
    :param layout_body: expand nested :#block: forms in each rule body
    :return: list of rule row cons forms
    """

    rules = []
    for rule_source in unpack(parent_body):
        rules.append(_rule_row(reader, rule_source, layout_body))
    return rules


def _lower_rules(reader, source, peers, head_sym, head_matchers,
                 with_literals, layout_body=False):
    """
    Lower a transformer block with raw rule children

    :param reader: the LayoutReader expanding the source
    :param source: pass-one block source for the transformer block
    :param peers: peer block sources, must be empty
    :param head_sym: emitted form head symbol
    :param head_matchers: matcher functions for the block header, tried
        in order
    :param with_literals: whether the header includes a literals list
    :param layout_body: expand nested :#block: forms in each rule body
    :return: lowered transformer form
    """

    if peers:
        raise LayoutError(
            f'{head_sym} block does not take peers',
            get_position(source))

    parent_header, parent_body = source

    _index, head_m = first_match(cons(parent_header, nil), head_matchers)
    if head_m is None:
        if with_literals:
            raise LayoutError(
                f'{head_sym} requires literals list and at least one rule',
                get_position(source))
        raise LayoutError(
            f'{head_sym} requires at least one rule',
            get_position(source))

    rules = _rule_rows(reader, parent_body, layout_body)

    if not rules:
        raise LayoutError(
            f'{head_sym} requires at least one rule',
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
def layout_syntax_rules(reader, source, peers):
    """
    Lower syntax-rules block to ``(syntax-rules (LIT ...) (PATTERN . TMPL...) ...)``.

    Each raw rule child supplies the pattern column from its header and
    template forms from its body.
    """

    return _lower_rules(
        reader, source, peers, _symbol_syntax_rules,
        _RULES_HEAD_MATCHERS, True)


@builtin('layout/bare-syntax')
@layout(symbol('layout/bare-syntax'), raw=True)
def layout_bare_syntax(reader, source, peers):
    """
    Lower bare-syntax block to ``(bare-syntax SLOT TMPL)``, or with a
    leading name in the header, ``(bare-syntax NAME SLOT TMPL)``; a
    multi-form template body is wrapped in ``begin``.

    The header names the called-by slot; the body is expanded like any
    block body, then folded, before it becomes the template.
    """

    if peers:
        raise LayoutError(
            'bare-syntax block does not take peers',
            get_position(source))

    header, body = source

    _index, head_m = first_match(cons(header, nil), _BARE_HEAD_MATCHERS)
    if head_m is None:
        raise LayoutError(
            'bare-syntax requires a called-by symbol',
            get_position(source))

    if is_nil(body):
        raise LayoutError(
            'bare-syntax requires a template',
            get_position(source))

    from . import apply_lines

    body = reader.fold_lines(apply_lines(body, reader))
    if not is_nil(body) and is_pair(body) and is_nil(body.cdr):
        template = body.car
    else:
        template = cons(_symbol_begin, body)
    ret = cons(template, nil)
    ret = cons(head_m['slot'].datum, ret)

    if 'name' in head_m:
        ret = cons(head_m['name'].datum, ret)

    return cons(_symbol_bare_syntax, ret)


@builtin('layout/macro-rules')
@layout(symbol('layout/macro-rules'), raw=True)
def layout_macro_rules(reader, source, peers):
    """
    Lower macro-rules block to ``(macro-rules (LIT ...) (PATTERN . BODY...) ...)``.

    Same block shape as syntax-rules; only the emitted head differs.
    """

    return _lower_rules(
        reader, source, peers, _symbol_macro_rules,
        _RULES_HEAD_MATCHERS, True, layout_body=True)


@builtin('layout/special-rules')
@layout(symbol('layout/special-rules'), raw=True)
def layout_special_rules(reader, source, peers):
    """
    Lower special-rules block to ``(special-rules (LIT ...) (PATTERN . BODY...) ...)``.

    Same block shape as macro-rules; only the emitted head differs.
    """

    return _lower_rules(
        reader, source, peers, _symbol_special_rules,
        _RULES_HEAD_MATCHERS, True, layout_body=True)


@builtin('layout/syntax-match')
@layout(symbol('layout/syntax-match'), raw=True)
def layout_syntax_match(reader, source, peers):
    """
    Lower syntax-match block to ``(syntax-match (PATTERN . TMPL...) ...)``.

    No literals list; each raw rule child supplies pattern and template forms.
    """

    return _lower_rules(
        reader, source, peers, _symbol_syntax_match,
        _MATCH_HEAD_MATCHERS, False)


@builtin('layout/macro-match')
@layout(symbol('layout/macro-match'), raw=True)
def layout_macro_match(reader, source, peers):
    """
    Lower macro-match block to ``(macro-match (PATTERN . BODY...) ...)``.

    Same rule shape as macro-rules blocks.
    """

    return _lower_rules(
        reader, source, peers, _symbol_macro_match,
        _MATCH_HEAD_MATCHERS, False, layout_body=True)


@builtin('layout/special-match')
@layout(symbol('layout/special-match'), raw=True)
def layout_special_match(reader, source, peers):
    """
    Lower special-match block to ``(special-match (PATTERN . BODY...) ...)``.

    Same rule shape as macro-match blocks.
    """

    return _lower_rules(
        reader, source, peers, _symbol_special_match,
        _MATCH_HEAD_MATCHERS, False, layout_body=True)


# The end.
