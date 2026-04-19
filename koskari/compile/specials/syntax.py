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
from ...lib import cons, nil, pair, symbol
from ...matcher import (BIND, DEFINE, LIST, MATCH, REPEAT, REST, VALUE, match,
                        matcher_slot_names)
from ...syntax import (SyntaxRule, SyntaxRulesTransformer, _serialize_form,
                       build_syntax_rules_transformer,
                       parse_explicit_rule_pattern, parse_macro_rules_rows)
from ...types import build_proper, get_position, is_pair
from ..compiler import Compiler
from ..helpers import special, special_forms


_SYNTAX_MATCH_RULE_ROW = LIST(BIND("pattern"), VALUE("template"))
SYNTAX_MATCH_RULES_PATTERN = MATCH(REPEAT("rules", _SYNTAX_MATCH_RULE_ROW))

_MACRO_MATCH_RULE_ROW = LIST(BIND("pattern"), REST("body"))
MACRO_MATCH_RULES_PATTERN = MATCH(REPEAT("rules", _MACRO_MATCH_RULE_ROW))

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

    Lowers to a (build-special-rules ...) call form for recompilation. The
    built transformer is the value of that expression; use (compile/define
    NAME ...) to bind it.
    """

    _, rest = source

    matched = match(rest, _COMPILE_NAME_LITERALS_RULES)
    if matched is None:
        raise SpecialError(
            "compile/special-rules requires name, literals list, and rule forms",
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

    return cons(symbol("build-special-rules"),
                str(namesym), literals_tuple,
                *parts, nil)


@special_forms(_COMPILE_NAME_RULES_TAIL)
@special(symbol("compile/special-match"))
def special_compile_special_match(compiler, source, tc=False):
    """
    Compile (compile/special-match NAME RULE ...).

    Rule rows are (PATTERN BODY...); PATTERN is passed to
    parse_explicit_rule_pattern.

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

        pattern_matcher = parse_explicit_rule_pattern(pattern_src)
        vars_ordered = [symbol(n) for n in matcher_slot_names(pattern_matcher)]

        parts.append(pattern_matcher.to_tuple())
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(*vars_ordered), body_src))

    return cons(symbol("build-special-rules"),
                str(namesym), (), *parts, nil)


# The end.
