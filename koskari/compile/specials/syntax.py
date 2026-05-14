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

Special forms for use with syntax, macro, and special rules. Lowers from
convenience syntax to runtime call forms of builder functions.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
"""


from ...exceptions import SpecialError
from ...lib import Datum, cons, gensym, nil, pair, symbol
from ...matcher import (BIND, DEFINE, LIST, LITERAL_SYM, MATCH, REST, SYMBOL,
                        VALUE, parse_explicit_rule_pattern, pattern_slot_names,
                        repeat_capture)
from ...stdlib import builtin
from ...syntax import (Syntax, SyntaxTransform, parse_syntax_rules_pattern)
from ...syntax import serialize_form as _serialize_form
from ...syntax import special
from ...types import (build_proper, get_position, is_pair, is_proper,
                      is_symbol, unpack)
from ..compiler import Compiler
from ..helpers import match, match1


_sym_compile_quote = symbol("compile/quote")
_sym_parse_rule_pattern = symbol("parse-rule-pattern")
_sym_parse_match_pattern = symbol("parse-match-pattern")


def _emit_parse_rule_pattern(pattern_src, literals_datum):
    """
    Emit ``(parse-rule-pattern (compile/quote PATTERN) (compile/quote LITS))``
    """

    return cons(
        _sym_parse_rule_pattern,
        cons(_sym_compile_quote, pattern_src, nil),
        cons(_sym_compile_quote, literals_datum, nil),
        nil)


def _emit_parse_match_pattern(pattern_src):
    """
    Emit ``(parse-match-pattern (compile/quote PATTERN))``
    """

    return cons(
        _sym_parse_match_pattern,
        cons(_sym_compile_quote, pattern_src, nil),
        nil)


_SYNTAX_MATCH_RULE_ROW = LIST(BIND("pattern"), VALUE("template"))
SYNTAX_MATCH_RULES_PATTERN = MATCH(
    repeat_capture("rules", _SYNTAX_MATCH_RULE_ROW))

# Opaque one-cell rows for compile-time parsing of ``syntax-rules`` /
# ``macro-rules`` / ``special-rules`` surfaces (Ki rules are not always
# two-element ``(pattern template)`` lists). Inner REPEAT binds rename to
# ``rule__sr`` in the native matcher.
_SYNTAX_RULES_BODY_PATTERN = MATCH(repeat_capture("rules", BIND("rule")))

_MACRO_MATCH_RULE_ROW = LIST(BIND("pattern"), REST("body"))
MACRO_MATCH_RULES_PATTERN = MATCH(
    repeat_capture("rules", _MACRO_MATCH_RULE_ROW))

_MACRO_RULES_BODY_PATTERN = MATCH(repeat_capture("rules", BIND("rule")))

_MATCH_FORM_PATTERN = LIST(LITERAL_SYM("match"), VALUE("inner"))

SPECIAL_RULES_PATTERN = MATCH(
    repeat_capture("rules", _MACRO_MATCH_RULE_ROW))
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


def _opaque_rule_mr(frame):
    """
    :param frame: repeat frame mapping slot names to helper match-result cells
    :return: the cell wrapper for one opaque rule row
    :raises SpecialError: when the expected slot is missing
    """

    r = frame.get("rule")
    if r is not None:
        return r
    r = frame.get("rule__sr")
    if r is not None:
        return r
    raise SpecialError("internal: missing rule cell in rules row", frame)


def _opaque_rule_spine(frame):
    """
    One opaque rule row as a koskari form (normally a pair spine), after
    peeling a stray inner capture dict some native REPEAT/BIND combinations
    attach
    """

    src = _opaque_rule_mr(frame).datum
    if isinstance(src, dict):
        if len(src) == 1:
            (k, v), = src.items()
            if k in ("rule", "rule__sr"):
                if isinstance(v, Datum):
                    return v.datum
                return v
        inner = src.get("rule") or src.get("rule__sr")
        if inner is not None:
            if isinstance(inner, Datum):
                return inner.datum
            return inner
    return src


def _syntax_rules_row_pattern_and_template(raw_rule):
    """
    Split one syntax-rules row into pattern and template datums.

    Rows from the reader layout are normally ``(pattern-column template)`` of
    length two (same as :func:`build_syntax_rules_transformer` historically
    required). Ki sources can also supply longer proper lists; then the
    template is the last top-level form and all preceding forms are grouped
    like :func:`koskari.reader.layouts.define_syntax._pattern_template_from_rule`.
    A pattern column ``(match INNER)`` is unwrapped to ``INNER`` for RULE
    lowering.
    """

    if not is_pair(raw_rule):
        raise SpecialError("invalid syntax-rules rule", raw_rule)
    if not is_proper(raw_rule):
        raise SpecialError("syntax-rules rule must be a proper list", raw_rule)
    rule_parts = list(unpack(raw_rule))
    if len(rule_parts) < 2:
        raise SpecialError(
            "syntax-rules rule must have pattern and template", raw_rule)
    if len(rule_parts) == 2:
        pattern_src, template_expr = rule_parts[0], rule_parts[1]
    else:
        pattern_src = cons(*rule_parts[:-1], nil)
        template_expr = rule_parts[-1]
    match_m = match1(pattern_src, [_MATCH_FORM_PATTERN])
    if match_m is not None:
        pattern_src = match_m["inner"].datum
    return pattern_src, template_expr


_MAKE_SYNTAX_PATTERNS = [
    MATCH(SYMBOL("_"), REST("rest")),
]


@builtin("compile/make-syntax")
@special("compile/make-syntax", patterns=_MAKE_SYNTAX_PATTERNS)
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

    inner = matched["expr"].datum
    pos = get_position(inner) if is_pair(inner) else get_position(source)

    return cons(symbol("syntax"), inner, pos, nil)


_COMPILE_NAME_SPEC_PATTERNS = [MATCH(DEFINE("name"), VALUE("spec"))]


_SYNTAX_RULES_PATTERNS = [
    MATCH(SYMBOL("_"), DEFINE("name"), VALUE("literals"), REST("rules")),
]


@builtin("compile/syntax-rules")
@special("compile/syntax-rules", patterns=_SYNTAX_RULES_PATTERNS)
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

    namesym = matched["name"].datum

    transformer = build_syntax_rules_transformer(
        namesym,
        matched["literals"].datum,
        matched["rules"].datum,
    )
    tagged = transformer.to_syntax_rules_tagged()
    return cons(symbol("build-syntax-rules"), str(namesym), tagged, nil)


_MACRO_RULES_PATTERNS = [
    MATCH(SYMBOL("_"), DEFINE("name"), VALUE("literals"), REST("rules")),
]


@builtin("compile/macro-rules")
@special("compile/macro-rules", patterns=_MACRO_RULES_PATTERNS)
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

    namesym = matched["name"].datum

    literals, rows = parse_macro_rules_rows(
        namesym,
        matched["literals"].datum,
        matched["rules"].datum,
    )
    literals_tuple = tuple(
        _serialize_form(x) for x in sorted(literals, key=str))

    parts = []

    for pattern_matcher, pattern_src, formals, body_forms in rows:
        parts.append(_emit_parse_rule_pattern(
            pattern_src, build_proper(*sorted(literals, key=str))))
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(*formals), body_forms))

    return cons(symbol("build-macro-rules"),
                str(namesym), literals_tuple,
                *parts, nil)


_SYNTAX_MATCH_PATTERNS = [
    MATCH(SYMBOL("_"), DEFINE("name"), REST("rules")),
]


@builtin("compile/syntax-match")
@special("compile/syntax-match", patterns=_SYNTAX_MATCH_PATTERNS)
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

    namesym = matched["name"].datum

    rules_expr = matched["rules"].datum
    literals = frozenset({namesym})

    # RULES is a list of (PATTERN TEMPLATE) rows.
    rules_m = match(rules_expr, SYNTAX_MATCH_RULES_PATTERN)
    if rules_m is None:
        raise SpecialError(
            "syntax-match requires at least one valid rule", rules_expr)

    transforms = []
    for sub_res in rules_m["rules"]:
        pattern_src = sub_res["pattern"].datum
        template_expr = sub_res["template"].datum
        pattern_matcher = parse_explicit_rule_pattern(pattern_src)
        transforms.append(SyntaxTransform(
            pattern_matcher, template_expr, pattern_src,
            syntax_match_dialect=True))

    # Same emission path as compile/syntax-rules: one build-syntax-rules call.
    transformer = Syntax(
        namesym, (), tuple(transforms), serialized_literals=literals)
    tagged = transformer.to_syntax_rules_tagged()

    return cons(symbol("build-syntax-rules"), str(namesym), tagged, nil)


_MACRO_MATCH_PATTERNS = [
    MATCH(SYMBOL("called-by"), DEFINE("name"), REST("rules")),
]


@builtin("compile/macro-match")
@special("compile/macro-match", patterns=_MACRO_MATCH_PATTERNS)
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

    namesym = matched["name"].datum

    rules_expr = matched["rules"].datum

    rules_m = match(rules_expr, MACRO_MATCH_RULES_PATTERN)
    if rules_m is None:
        raise SpecialError(
            "macro-match requires at least one valid rule", rules_expr)

    parts = []

    for sub_res in rules_m["rules"]:
        pattern_src = sub_res["pattern"].datum
        body_src = sub_res["body"].datum

        pattern_matcher = parse_explicit_rule_pattern(pattern_src)
        vars_ordered = [symbol(n) for n in pattern_slot_names(pattern_matcher)]

        parts.append(_emit_parse_match_pattern(pattern_src))
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(*vars_ordered), body_src))

    return cons(symbol("build-macro-rules"),
                str(namesym), (), *parts, nil)


_SPECIAL_RULES_PATTERNS = [
    MATCH(SYMBOL("_"), DEFINE("name"), VALUE("literals"), REST("rules")),
]


@builtin("compile/special-rules")
@special("compile/special-rules", patterns=_SPECIAL_RULES_PATTERNS)
def special_compile_special_rules(compiler, source, tc=False):
    """
    Compile (compile/special-rules NAME LITERALS RULES...).

    Each rule pattern column is ``(compiler-formal pattern [tc-formal])``: the
    first subform names the compiler parameter (any symbol), the second is the
    nested RULE pattern, the optional third names the ``tc`` formal. PATTERN is
    lowered with parse_syntax_rules_pattern. Lowers to a (build-special-rules ...)
    call form for recompilation. The built transformer is the value of that
    expression; use (compile/define NAME ...) to bind it.
    """

    _, rest = source

    matched = match(rest, _COMPILE_NAME_LITERALS_RULES)
    if matched is None:
        raise SpecialError(
            "compile/special-rules requires name, literals list, and rule forms",
            source)

    namesym = matched["name"].datum
    literals_expr = matched["literals"].datum
    rules_expr = matched["rules"].datum

    literals = frozenset(unpack(literals_expr))
    literals = literals | {namesym}

    rules_m = match(rules_expr, _MACRO_RULES_BODY_PATTERN)
    if rules_m is None or not rules_m["rules"]:
        raise SpecialError("special-rules requires at least one rule", rules_expr)

    literals_tuple = tuple(
        _serialize_form(x) for x in sorted(literals, key=str))

    parts = []
    for res in rules_m["rules"]:
        raw_rule = _opaque_rule_spine(res)
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

        compiler_formal = m["compiler"].datum
        inner = m["pattern"].datum
        tc_formal = m["tc"].datum if "tc" in m else gensym()

        pattern_matcher = parse_syntax_rules_pattern(inner, literals, raw_rule)
        vars_ordered = collect_pattern_variable_symbols(inner, literals)

        parts.append(_emit_parse_rule_pattern(
            inner, build_proper(*sorted(literals, key=str))))
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(compiler_formal, tc_formal, *vars_ordered),
                          body_forms))

    return cons(symbol("build-special-rules"),
                str(namesym), literals_tuple,
                *parts, nil)


_SPECIAL_MATCH_PATTERNS = [
    MATCH(SYMBOL("called-by"), DEFINE("name"), REST("rules")),
]


@builtin("compile/special-match")
@special("compile/special-match", patterns=_SPECIAL_MATCH_PATTERNS)
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

    namesym = matched["name"].datum

    rules_expr = matched["rules"].datum

    rules_m = match(rules_expr, MACRO_MATCH_RULES_PATTERN)
    if rules_m is None:
        raise SpecialError(
            "special-match requires at least one valid rule", rules_expr)

    parts = []

    for sub_res in rules_m["rules"]:
        pattern_src = sub_res["pattern"].datum
        body_src = sub_res["body"].datum

        m = match1(pattern_src, SPECIAL_PATTERN_BREAKOUT)
        if m is None:
            raise SpecialError(
                "special-match pattern column must be "
                "(compiler-formal pattern [tc-formal])",
                pattern_src)

        compiler_formal = m["compiler"].datum
        inner = m["pattern"].datum
        if is_symbol(inner):
            inner = cons(cons(symbol("REST"), inner, nil), nil)
        tc_formal = m["tc"].datum if "tc" in m else gensym()

        pattern_matcher = parse_explicit_rule_pattern(inner)
        vars_ordered = [symbol(n) for n in pattern_slot_names(pattern_matcher)]

        parts.append(_emit_parse_match_pattern(inner))
        parts.append(cons(symbol("compile/lambda"),
                          build_proper(compiler_formal, tc_formal, *vars_ordered),
                          body_src))

    return cons(symbol("build-special-rules"),
                str(namesym), (), *parts, nil)


def build_syntax_rules_transformer(
        name, literals_expr, rules_expr, *, match_patterns=False):
    """
    Build a :class:`koskari.syntax.Syntax` from literals and rule forms.

    :param match_patterns: If True, each rule's pattern column is lowered with
        :func:`parse_explicit_rule_pattern` (MATCH dialect). If False (default),
        use :func:`parse_syntax_rules_pattern` (syntax-rules / ``(match ...)``
        escape).
    """

    literals = frozenset(unpack(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _SYNTAX_RULES_BODY_PATTERN)
    if rules_m is None or not rules_m["rules"]:
        raise SpecialError("syntax-rules requires at least one rule", rules_expr)

    transforms = []
    for res in rules_m["rules"]:
        raw_rule = _opaque_rule_spine(res)
        pattern_src, template_expr = _syntax_rules_row_pattern_and_template(
            raw_rule)
        if match_patterns:
            pattern_matcher = parse_explicit_rule_pattern(pattern_src)
            transforms.append(SyntaxTransform(
                pattern_matcher, template_expr, pattern_src,
                syntax_match_dialect=True))
        else:
            pattern_matcher = parse_syntax_rules_pattern(
                pattern_src, literals, raw_rule)
            transforms.append(SyntaxTransform(
                pattern_matcher, template_expr, pattern_src,
                syntax_match_dialect=False))

    return Syntax(name, (), tuple(transforms), serialized_literals=literals)


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
        pattern_src, formals_tuple, list of body forms)``
    :raises SpecialError: when the rules are not valid macro-rules rows
    """

    literals = frozenset(unpack(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _MACRO_RULES_BODY_PATTERN)
    if rules_m is None or not rules_m["rules"]:
        raise SpecialError("macro-rules requires at least one rule", rules_expr)

    rows = []
    for res in rules_m["rules"]:
        raw_rule = _opaque_rule_spine(res)
        if not is_pair(raw_rule):
            raise SpecialError("invalid macro-rules rule", raw_rule)

        if not is_proper(raw_rule):
            raise SpecialError("macro-rules rule must be a proper list", raw_rule)

        pattern_src, body_forms = raw_rule
        if not body_forms:
            raise SpecialError("macro-rules rule requires a body", raw_rule)

        pattern_matcher = parse_syntax_rules_pattern(
            pattern_src, literals, raw_rule)
        vars_ordered = collect_pattern_variable_symbols(pattern_src, literals)
        rows.append((
            pattern_matcher, pattern_src, tuple(vars_ordered), body_forms))

    return literals, tuple(rows)


# The end.
