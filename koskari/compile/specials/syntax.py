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

from ...exceptions import SpecialError
from ...lib import cons, gensym, nil, pair, symbol
from ...matcher import (BIND, DEFINE, LIST, LITERAL, MATCH, MATCHERS, REPEAT,
                        REST, SYMBOL, VALUE, match, matcher_slot_names,
                        match1)
from ...syntax import SyntaxRule, SyntaxRulesTransformer, _serialize_form
from ...types import (build_proper, get_position, is_keyword, is_pair,
                      is_proper, is_symbol, unpack)
from ..compiler import Compiler
from ..helpers import special, special_forms


_SYNTAX_MATCH_RULE_ROW = LIST(BIND("pattern"), VALUE("template"))
SYNTAX_MATCH_RULES_PATTERN = MATCH(REPEAT("rules", _SYNTAX_MATCH_RULE_ROW))

_MACRO_MATCH_RULE_ROW = LIST(BIND("pattern"), REST("body"))
MACRO_MATCH_RULES_PATTERN = MATCH(REPEAT("rules", _MACRO_MATCH_RULE_ROW))

SPECIAL_RULES_PATTERN = MATCH(REPEAT("rules", _MACRO_MATCH_RULE_ROW))
SPECIAL_PATTERN_BREAKOUT = [
    MATCH(LIST(SYMBOL("compiler"), VALUE("pattern"), SYMBOL("tc"))),
    MATCH(LIST(SYMBOL("compiler"), VALUE("pattern"))),
]

# After compile/define-syntax, (compile/<rules-kind> NAME LITERALS . RULES).
_COMPILE_NAME_LITERALS_RULES = [
    MATCH(DEFINE("name"), VALUE("literals"), REST("rules")),
]

# After compile/define-syntax, (compile/<match-kind> NAME . RULES).
_COMPILE_NAME_RULES_TAIL = [
    MATCH(DEFINE("name"), REST("rules")),
]


@special(symbol("compile/make-syntax"))
def special_compile_syntax(
        compiler: Compiler,
        source: pair,
        tc: bool = False):
    """
    Compile (compile/make-syntax EXPR). Evaluates EXPR and wraps the result in
    ``Syntax`` with position from the expression form when available.
    """

    # Rest must be exactly one expression; emit (syntax EXPR [POS]) for the
    # runtime wrapper.
    _, rest = source
    matched = match(rest, [MATCH(VALUE("expr"))])
    if matched is None:
        raise SpecialError("make-syntax requires exactly one expression", source)

    inner = matched["expr"].source
    pos = get_position(inner) if is_pair(inner) else get_position(source)

    return cons(symbol("syntax"), inner, pos, nil)


_COMPILE_NAME_SPEC_PATTERNS = [MATCH(DEFINE("name"), VALUE("spec"))]


@special_forms(_COMPILE_NAME_LITERALS_RULES)
@special(symbol("compile/syntax-rules"))
def special_compile_syntax_rules(compiler, source, tc=False):
    """
    Compile (compile/syntax-rules NAME LITERALS RULES...).

    Lowers to a (build-syntax-rules ...) call form for recompilation. The
    built transformer is the value of that expression; use (compile/define
    NAME ...) to bind it.
    """

    _, rest = source
    matched = match(rest, _COMPILE_NAME_LITERALS_RULES)
    if matched is None:
        raise SpecialError(
            "compile/syntax-rules requires name, literals list, and rule forms",
            source)

    namesym = matched["name"].source

    transformer = build_syntax_rules_transformer(
        namesym,
        matched["literals"].source,
        matched["rules"].source,
    )
    tagged = transformer.to_tuple()
    return cons(symbol("build-syntax-rules"), str(namesym), tagged, nil)


@special_forms(_COMPILE_NAME_LITERALS_RULES)
@special(symbol("compile/macro-rules"))
def special_compile_macro_rules(compiler, source, tc=False):
    """
    Compile (compile/macro-rules NAME LITERALS RULES...).

    Lowers to a (build-macro-rules ...) call form for recompilation. The
    built transformer is the value of that expression; use (compile/define
    NAME ...) to bind it.
    """

    _, rest = source

    matched = match(rest, _COMPILE_NAME_LITERALS_RULES)
    if matched is None:
        raise SpecialError(
            "compile/macro-rules requires name, literals list, and rule forms",
            source)

    namesym = matched["name"].source

    literals, rows = parse_macro_rules_rows(
        namesym,
        matched["literals"].source,
        matched["rules"].source,
    )
    literals_tuple = tuple(
        _serialize_form(x) for x in sorted(literals, key=str))

    parts = []

    for pattern_matcher, formals, body_forms in rows:
        parts.append(pattern_matcher.to_tuple())
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(*formals), body_forms))

    return cons(symbol("build-macro-rules"),
                str(namesym), literals_tuple,
                *parts, nil)


@special_forms(_COMPILE_NAME_RULES_TAIL)
@special(symbol("compile/syntax-match"))
def special_compile_syntax_match(compiler, source, tc=False):
    """
    Compile (compile/syntax-match NAME RULE ...).

    Rule rows are (PATTERN TEMPLATE); PATTERN is passed to
    parse_explicit_rule_pattern.

    Lowers to a (build-syntax-rules ...) call form for recompilation. The
    built transformer is the value of that expression; use (compile/define
    NAME ...) to bind it.
    """

    _, rest = source
    matched = match(rest, _COMPILE_NAME_RULES_TAIL)
    if matched is None:
        raise SpecialError(
            "compile/syntax-match requires name and at least one rule",
            source)

    namesym = matched["name"].source

    rules_expr = matched["rules"].source
    literals = frozenset({namesym})

    # RULES is a list of (PATTERN TEMPLATE) rows.
    rules_m = match(rules_expr, SYNTAX_MATCH_RULES_PATTERN)
    if rules_m is None:
        raise SpecialError(
            "syntax-match requires at least one valid rule", rules_expr)

    rules = []
    for sub_res, _sp in rules_m["rules"].matches:
        pattern_src = sub_res["pattern"].source
        template_expr = sub_res["template"].source
        pattern_matcher = parse_explicit_rule_pattern(pattern_src)
        rules.append(SyntaxRule(pattern_matcher, template_expr))

    # Same emission path as compile/syntax-rules: one build-syntax-rules call.
    transformer = SyntaxRulesTransformer(namesym, literals, tuple(rules))
    tagged = transformer.to_tuple()

    return cons(symbol("build-syntax-rules"), str(namesym), tagged, nil)


@special_forms(_COMPILE_NAME_RULES_TAIL)
@special(symbol("compile/macro-match"))
def special_compile_macro_match(compiler, source, tc=False):
    """
    Compile (compile/macro-match NAME RULE ...).

    Rule rows are (PATTERN BODY...); PATTERN is passed to
    parse_explicit_rule_pattern.

    Lowers to a (build-macro-rules ...) call form for recompilation. The
    built transformer is the value of that expression; use (compile/define
    NAME ...) to bind it.
    """

    _, rest = source
    matched = match(rest, _COMPILE_NAME_RULES_TAIL)
    if matched is None:
        raise SpecialError(
            "compile/macro-match requires name and at least one rule",
            source)

    namesym = matched["name"].source

    rules_expr = matched["rules"].source

    rules_m = match(rules_expr, MACRO_MATCH_RULES_PATTERN)
    if rules_m is None:
        raise SpecialError(
            "macro-match requires at least one valid rule", rules_expr)

    parts = []

    for sub_res, _sp in rules_m["rules"].matches:
        pattern_src = sub_res["pattern"].source
        body_src = sub_res["body"].source

        pattern_matcher = parse_explicit_rule_pattern(pattern_src)
        vars_ordered = [symbol(n) for n in matcher_slot_names(pattern_matcher)]

        parts.append(pattern_matcher.to_tuple())
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(*vars_ordered), body_src))

    return cons(symbol("build-macro-rules"),
                str(namesym), (), *parts, nil)


@special_forms(_COMPILE_NAME_LITERALS_RULES)
@special(symbol("compile/special-rules"))
def special_compile_special_rules(compiler, source, tc=False):
    """
    Compile (compile/special-rules NAME LITERALS RULES...).

    Each rule pattern column is ``(compiler-formal pattern [tc-formal])``: the
    first subform names the compiler parameter (any symbol), the second is the
    nested RULE pattern, the optional third names the ``tc`` formal. PATTERN is
    lowered with parse_rule_pattern. Lowers to a (build-special-rules ...)
    call form for recompilation. The built transformer is the value of that
    expression; use (compile/define NAME ...) to bind it.
    """

    _, rest = source

    matched = match(rest, _COMPILE_NAME_LITERALS_RULES)
    if matched is None:
        raise SpecialError(
            "compile/special-rules requires name, literals list, and rule forms",
            source)

    namesym = matched["name"].source
    literals_expr = matched["literals"].source
    rules_expr = matched["rules"].source

    literals = frozenset(unpack(literals_expr))
    literals = literals | {namesym}

    rules_m = match(rules_expr, _MACRO_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise SpecialError("special-rules requires at least one rule", rules_expr)

    literals_tuple = tuple(
        _serialize_form(x) for x in sorted(literals, key=str))

    parts = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise SpecialError("invalid special-rules rule", raw_rule)

        if not is_proper(raw_rule):
            raise SpecialError("special-rules rule must be a proper list", raw_rule)

        pattern_src, body_forms = raw_rule
        if not body_forms:
            raise SpecialError("special-rules rule requires a body", raw_rule)

        m = match1(pattern_src, SPECIAL_PATTERN_BREAKOUT)
        if m is None:
            raise SpecialError(
                "special-rules pattern column must be "
                "(compiler-formal pattern [tc-formal])",
                pattern_src)

        compiler_formal = m["compiler"].source
        inner = m["pattern"].source
        tc_formal = m["tc"].source if "tc" in m else gensym()

        pattern_matcher = parse_rule_pattern(inner, literals, raw_rule)
        vars_ordered = collect_pattern_variable_symbols(inner, literals)

        parts.append(pattern_matcher.to_tuple())
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(compiler_formal, tc_formal, *vars_ordered),
                          body_forms))

    return cons(symbol("build-special-rules"),
                str(namesym), literals_tuple,
                *parts, nil)


@special_forms(_COMPILE_NAME_RULES_TAIL)
@special(symbol("compile/special-match"))
def special_compile_special_match(compiler, source, tc=False):
    """
    Compile (compile/special-match NAME RULE ...).

    Each rule pattern column is ``(compiler-formal pattern [tc-formal])``: the
    first subform names the compiler parameter (any symbol), the second is the
    nested MATCH pattern for parse_explicit_rule_pattern, the optional third
    names the ``tc`` formal.

    Lowers to a (build-special-rules ...) call form for recompilation (same
    shape as compile/macro-match). The built transformer is the value of that
    expression; use (compile/define NAME ...) to bind it.
    """

    _, rest = source
    matched = match(rest, _COMPILE_NAME_RULES_TAIL)
    if matched is None:
        raise SpecialError(
            "compile/special-match requires name and at least one rule",
            source)

    namesym = matched["name"].source

    rules_expr = matched["rules"].source

    rules_m = match(rules_expr, MACRO_MATCH_RULES_PATTERN)
    if rules_m is None:
        raise SpecialError(
            "special-match requires at least one valid rule", rules_expr)

    parts = []

    for sub_res, _sp in rules_m["rules"].matches:
        pattern_src = sub_res["pattern"].source
        body_src = sub_res["body"].source

        m = match1(pattern_src, SPECIAL_PATTERN_BREAKOUT)
        if m is None:
            raise SpecialError(
                "special-match pattern column must be "
                "(compiler-formal pattern [tc-formal])",
                pattern_src)

        compiler_formal = m["compiler"].source
        inner = m["pattern"].source
        if is_symbol(inner):
            inner = cons(cons(symbol("REST"), inner, nil), nil)
        tc_formal = m["tc"].source if "tc" in m else gensym()

        pattern_matcher = parse_explicit_rule_pattern(inner)
        vars_ordered = [symbol(n) for n in matcher_slot_names(pattern_matcher)]

        parts.append(pattern_matcher.to_tuple())
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(compiler_formal, tc_formal, *vars_ordered),
                          body_src))

    return cons(symbol("build-special-rules"),
                str(namesym), (), *parts, nil)


_SYNTAX_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))

_MACRO_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))


def build_syntax_rules_transformer(
        name, literals_expr, rules_expr, *, match_patterns=False):
    """
    Build a :class:`SyntaxRulesTransformer` from literals and rule forms.

    :param match_patterns: If True, each rule's pattern column is lowered with
        :func:`parse_explicit_rule_pattern` (MATCH dialect). If False (default),
        use :func:`parse_rule_pattern` (syntax-rules / ``(match ...)`` escape).
    """

    literals = frozenset(unpack(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _SYNTAX_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise SpecialError("syntax-rules requires at least one rule", rules_expr)

    rules = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise SpecialError("invalid syntax-rules rule", raw_rule)

        pattern_src, template_tail = raw_rule
        if not is_proper(template_tail) or template_tail.cdr is not nil:
            raise SpecialError(
                "syntax-rules rule must have pattern and template",
                raw_rule)
        template_expr = template_tail.car
        if match_patterns:
            pattern_matcher = parse_explicit_rule_pattern(pattern_src)
        else:
            pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
        rules.append(SyntaxRule(pattern_matcher, template_expr))

    return SyntaxRulesTransformer(name, literals, rules)


def collect_pattern_variable_symbols(pattern_expr, literals):
    """
    Pattern variables in pre-order, excluding literals and ellipsis markers.
    Each symbol appears once (first occurrence). Used for macro-rules formals.
    """

    seen = []

    def walk(expr):
        if expr is Ellipsis:
            return

        if is_symbol(expr):
            if expr in literals:
                return

            if expr not in seen:
                seen.append(expr)

            return

        if not is_pair(expr):
            return

        items = list(unpack(expr))
        i = 0
        while i < len(items):
            if i + 1 < len(items) and items[i + 1] is Ellipsis:
                walk(items[i])
                i += 2
            else:
                walk(items[i])
                i += 1

    walk(pattern_expr)
    return seen


def parse_macro_rules_rows(name, literals_expr, rules_expr):
    """
    Parse macro-rules literals and rules tail into matchers and body datums

    No compile/lambda or build-macro-rules source is emitted here; compile
    specials lower that from the returned rows.

    :param name: macro name symbol (included in literals)
    :param literals_expr: literals list expression (same position as in
        ``(macro-rules LITERALS RULE ...)``)
    :param rules_expr: rules tail (same as after literals in macro-rules form)
    :return: ``(literals, rows)`` where each row is ``(pattern_matcher,
        formals_tuple, list of body forms)``
    :raises SpecialError: when the rules are not valid macro-rules rows
    """

    literals = frozenset(unpack(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _MACRO_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise SpecialError("macro-rules requires at least one rule", rules_expr)

    rows = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise SpecialError("invalid macro-rules rule", raw_rule)

        if not is_proper(raw_rule):
            raise SpecialError("macro-rules rule must be a proper list", raw_rule)

        pattern_src, body_forms = raw_rule
        if not body_forms:
            raise SpecialError("macro-rules rule requires a body", raw_rule)

        pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
        vars_ordered = collect_pattern_variable_symbols(pattern_src, literals)
        rows.append((pattern_matcher, tuple(vars_ordered), body_forms))

    return literals, tuple(rows)


_symbol_match = symbol("match")


def parse_rule_pattern(pattern_expr, literals, source_ctx):
    """
    Dispatch **RULE** pattern source (``syntax-rules`` / ``macro-rules``).

    If the pattern is ``(match INNER)``, lower ``INNER`` with
    ``syntax_pattern_to_matcher``. Otherwise treat the whole tree as a
    Scheme-style pattern via ``syntax_pattern_to_matcher``.

    **MATCH** dialect patterns use :func:`parse_explicit_rule_pattern` at the
    call site (e.g. ``compile/syntax-match``), not via a ``rule`` wrapper here.
    """

    if is_pair(pattern_expr):
        head, rest = pattern_expr
        if head == _symbol_match:
            if not is_proper(rest) or rest.cdr is not nil:
                raise ValueError(
                    f"match pattern must have exactly one form: {pattern_expr}")

            return syntax_pattern_to_matcher(rest.car, literals, source_ctx)

    return syntax_pattern_to_matcher(pattern_expr, literals, source_ctx)


def syntax_pattern_to_matcher(pattern_expr, literals, source_ctx):
    """
    Lower a syntax-rules pattern cons tree to matcher objects.
    """

    if pattern_expr is Ellipsis:
        raise TypeError(f"invalid ellipsis placement in pattern: {pattern_expr}")

    if is_symbol(pattern_expr):
        if pattern_expr in literals:
            return MATCH(LITERAL(pattern_expr))
        return MATCH(REST(pattern_expr))

    if not is_pair(pattern_expr):
        return MATCH(LITERAL(pattern_expr))

    return MATCH(*_syntax_pattern_specs(
        list(unpack(pattern_expr)), literals, source_ctx))


def _syntax_elem_matcher(pat, literals, source_ctx):
    """
    Lower a single syntax-rules pattern element to matcher objects

    Symbols become LITERAL or VALUE; non-pairs become LITERAL; proper lists
    become LIST of nested specs from the same pattern language

    :param pat: pattern datum (symbol, pair, or literal)
    :param literals: literal symbols for this segment
    :param source_ctx: form for error reporting
    :raises ValueError: on invalid ellipsis placement
    :return: a matcher spec (LITERAL, VALUE, or LIST wrapping inner specs)
    """

    if pat is Ellipsis:
        raise ValueError(f"invalid ellipsis placement in pattern: {pat}")

    if is_symbol(pat):
        if pat in literals:
            return LITERAL(pat)
        return VALUE(pat)

    if not is_pair(pat):
        return LITERAL(pat)

    return LIST(*_syntax_pattern_specs(
        list(unpack(pat)), literals, source_ctx))


def _syntax_pattern_specs(p_items, literals, source_ctx):
    """
    Build matcher specs from a flat sequence of pattern list elements

    Treats ``(subpat ...)`` pairs with ``...`` as zero-or-more REPEAT regions;
    other elements are passed to :func:`_syntax_elem_matcher`

    :param p_items: list of elements from ``list(unpack(...))`` of a pattern list
    :param literals: literal symbols for this segment
    :param source_ctx: form for error reporting
    :return: list of matcher specs (LITERAL, VALUE, LIST, REPEAT, etc.)
    """

    specs = []
    i = 0
    while i < len(p_items):
        if i + 1 < len(p_items) and p_items[i + 1] is Ellipsis:
            pat = p_items[i]
            inner = _syntax_elem_matcher(pat, literals, source_ctx)
            # Bind REPEAT to the pattern variable when the repeated element is a
            # bare symbol (e.g. ``xs ...``); otherwise use a fresh slot. This
            # must match ``collect_pattern_variable_symbols`` so macro-rules
            # lambdas receive the correct keyword (see macro-rules ellipsis tests).
            if is_symbol(pat) and pat not in literals:
                slot = pat
            else:
                slot = gensym('_repeat')
            specs.append(REPEAT(slot, inner, min_count=0))
            i += 2
            continue

        specs.append(_syntax_elem_matcher(p_items[i], literals, source_ctx))
        i += 1

    return specs


def parse_explicit_rule_pattern(form):
    """
    Lower **MATCH** pattern source (a cons tree) to a matcher.

    Surface syntax: implicit literals for bare atoms; explicit declarations are
    proper lists ``(ATOM binding opt: val ...)`` where ``ATOM`` names a matcher
    in ``koskari.matcher.MATCHERS`` (via ``matcher/<lower>`` lookup on the
    symbol name). Distinct from **RULE** patterns
    (``syntax_pattern_to_matcher``).

    :param pattern_source: Match pattern only (not wrapped in ``rule``)
    :raises SpecialError: on invalid pattern shape or unknown matcher atom
    :return: Matcher (typically ``MATCH(...)`` wrapping the pattern sequence)
    """

    if form is Ellipsis:
        raise SpecialError(
            'invalid ellipsis in MATCH pattern', form)

    if not is_pair(form):
        return LITERAL(form)

    if not is_proper(form):
        raise SpecialError(
            'MATCH pattern must be a proper list', form)

    head, tail = form

    decl_cls = None
    decl_bind = None
    decl_opts = None

    if tail is not nil:
        decl_bind, decl_opts = tail
        reg_key = symbol('matcher/' + str(head).lower())
        if is_symbol(decl_bind) and reg_key in MATCHERS:
            decl_cls = MATCHERS[reg_key]

    if decl_cls is not None:
        kwargs = {}
        optwalk = decl_opts
        while optwalk is not nil:
            if not is_pair(optwalk):
                raise SpecialError(
                    'MATCH pattern: malformed matcher options',
                    form)
            keyw, rest = optwalk
            if rest is nil:
                raise SpecialError(
                    'MATCH pattern: matcher option missing value',
                    form)
            val, optwalk = rest
            if not is_keyword(keyw):
                raise SpecialError(
                    'MATCH pattern: expected keyword in matcher options',
                    form)
            kwargs[str(keyw)] = val
        if 'elem' in kwargs:
            ev = kwargs['elem']
            if is_pair(ev):
                kwargs['elem'] = parse_explicit_rule_pattern(ev)
        try:
            return decl_cls(decl_bind, **kwargs)
        except TypeError as exc:
            raise SpecialError(
                'MATCH pattern: invalid matcher arguments for %s'
                % (str(head),),
                form) from exc

    specs = []
    cur = form
    while cur is not nil:
        first, cur = cur
        specs.append(parse_explicit_rule_pattern(first))
    return MATCH(*specs)


# The end.
