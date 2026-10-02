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
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import cons, keyword, nil, symbol
from ...matcher import (
    BIND, BODY, CALLEDBY, LIST, MATCH, REPEAT, SYMBOL,
    VALUE, parse_explicit_rule_pattern, pattern_slot_names, repeat_capture)
from ...syntax import parse_syntax_rules_pattern, special
from ...types import (
    build_proper, is_nil, is_pair, is_proper, is_symbol, unpack)


_sym_quote_syntax = symbol("quote-syntax")
_sym_parse_rule_pattern = symbol("parse-rule-pattern")
_sym_parse_match_pattern = symbol("parse-match-pattern")


def _emit_parse_rule_pattern(pattern_src, literals_datum):
    """
    Emit ``(parse-rule-pattern (quote-syntax PATTERN) (quote-syntax LITS))``

    A set of literals is frozen so the emitted constant is hashable and
    serializable; every other literals shape passes through. The quoting
    keeps marks: a pattern or template an outer expansion introduced
    binds and resolves under the outer expansion's marks
    """

    if isinstance(literals_datum, set):
        literals_datum = frozenset(literals_datum)
    return cons(
        _sym_parse_rule_pattern,
        cons(_sym_quote_syntax, pattern_src, nil),
        cons(_sym_quote_syntax, literals_datum, nil),
        nil)


def _emit_parse_match_pattern(pattern_src):
    """
    Emit ``(parse-match-pattern (quote-syntax PATTERN))``
    """

    return cons(
        _sym_parse_match_pattern,
        cons(_sym_quote_syntax, pattern_src, nil),
        nil)


# Opaque one-cell rows for compile-time parsing of ``syntax-rules`` /
# ``macro-rules`` / ``syntax-match`` surfaces (Koskari rules are not
# always two-element ``(pattern template)`` lists), split later by
# _syntax_rules_row_pattern_and_template. Inner REPEAT binds rename to
# ``rule__sr`` in the native matcher.
_RULE_ROWS = repeat_capture("rules", REPEAT(BIND("rule"), min_count=1))

# Rows of ``(PATTERN BODY...)`` for the match-dialect surfaces
_MATCH_RULE_ROWS = repeat_capture(
    "rules",
    REPEAT(LIST(BIND("pattern"), BODY("body")), min_count=1))

# Rows of ``((COMPILER-FORMAL PATTERN) BODY...)`` for the special
# surfaces, whose pattern column names the compiler parameter
_SPECIAL_RULE_ROWS = repeat_capture(
    "rules",
    REPEAT(
        LIST(LIST(SYMBOL("compiler"), VALUE("pattern")), BODY("body")),
        min_count=1))

_SPECIAL_MATCH_RULE_ROWS = repeat_capture(
    "rules",
    REPEAT(
        LIST(LIST(SYMBOL("compiler"), VALUE("pattern")), BODY("body")),
        min_count=1))


def _syntax_rules_row_pattern_and_template(raw_rule):
    """
    Split one syntax-rules row into pattern and template datums.

    Rows from the reader layout are normally ``(pattern-column template)`` of
    length two (same shape ``compile/syntax-rules`` expects). Koskari sources
    can also supply longer proper lists; then the
    template is the last top-level form and all preceding forms are grouped
    like :func:`koskari.reader.layouts.define_syntax._pattern_template_from_rule`.
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
    return pattern_src, template_expr



_SYNTAX_RULES_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("name"),
        BIND("literals"),
        _RULE_ROWS),
]


@builtin("compile/syntax-rules")
@special("compile/syntax-rules", patterns=_SYNTAX_RULES_PATTERNS,
         unpack="kwargs")
def special_compile_syntax_rules(compiler, called_by, name, literals, rules):
    """
    Compile (compile/syntax-rules NAME LITERALS RULES...).

    Lowers to ``(build-syntax NAME-STR (parse-rule-pattern (quote-syntax PATTERN)
    (quote-syntax LITERALS-LIST)) (quote-syntax TEMPLATE) ...)`` for
    recompilation. ``NAME-STR`` is the macro name as a string. ``LITERALS-LIST``
    is the literals list form from the source, emitted as written; the macro
    name is not a literal. The head of each pattern lowers to a CALLEDBY
    capture under its own name, binding the symbol the macro was invoked as,
    so a template may use it. The built transformer is the value of that
    expression; use ``(compile/define NAME ...)`` to bind it. Pattern parsing
    runs when that expression is evaluated.
    """

    namesym = name.datum
    literals_expr = literals.datum

    parts = []
    for res in rules:
        raw_rule = res["rule"].datum
        pattern_src, template_expr = _syntax_rules_row_pattern_and_template(
            raw_rule)
        parts.append(_emit_parse_rule_pattern(pattern_src, literals_expr))
        parts.append(cons(_sym_quote_syntax, template_expr, nil))

    return cons(symbol("build-syntax"), str(namesym), *parts, nil)


_BARE_SYNTAX_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("name"),
        SYMBOL("slot"),
        VALUE("template")),
]


@builtin("compile/bare-syntax")
@special("compile/bare-syntax", patterns=_BARE_SYNTAX_PATTERNS,
         unpack="kwargs")
def special_compile_bare_syntax(compiler, called_by, name, slot, template):
    """
    Compile (compile/bare-syntax NAME SLOT TEMPLATE).

    A bare arm expands the syntax name where it appears as a lone atom
    rather than at the head of a call form. ``SLOT`` names the called-by
    capture, the only binding ``TEMPLATE`` may refer to.

    Lowers to ``(build-bare-syntax NAME-STR SLOT-STR (quote
    TEMPLATE))`` for recompilation. The result is a ``Syntax`` carrying
    only that arm; use ``(compile/define NAME ...)`` to bind it, or
    ``extend-syntax`` to attach it to an existing syntax.
    """

    return cons(symbol("build-bare-syntax"),
                str(name.datum), str(slot.datum),
                cons(_sym_quote_syntax, template.datum, nil),
                nil)


_MACRO_RULES_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("name"),
        BIND("literals"),
        _RULE_ROWS),
]


@builtin("compile/macro-rules")
@special("compile/macro-rules", patterns=_MACRO_RULES_PATTERNS,
         unpack="kwargs")
def special_compile_macro_rules(compiler, called_by, name, literals, rules):
    """
    Compile (compile/macro-rules NAME LITERALS RULES...).

    Lowers to ``(build-macro NAME-STR (parse-rule-pattern (quote-syntax PATTERN)
    (quote-syntax LITERALS-SET)) (lambda FORMALS . BODY) ...)`` for
    recompilation, where ``LITERALS-SET`` is the literals set (including the macro
    name) used for RULE lowering. The built transformer is the value of that
    expression; use ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum

    literal_set, rows = parse_macro_rules_rows(
        namesym,
        literals.datum,
        [res["rule"].datum for res in rules],
    )

    parts = []

    for pattern_matcher, pattern_src, formals, body_forms in rows:
        parts.append(_emit_parse_rule_pattern(pattern_src, literal_set))
        parts.append(cons(symbol("lambda"),
                          build_proper(*formals), body_forms))

    return cons(symbol("build-macro"), str(namesym), *parts, nil)


_SYNTAX_MATCH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("name"), _RULE_ROWS),
]


@builtin("compile/syntax-match")
@special("compile/syntax-match", patterns=_SYNTAX_MATCH_PATTERNS,
         unpack="kwargs")
def special_compile_syntax_match(compiler, called_by, name, rules):
    """
    Compile (compile/syntax-match NAME RULE ...).

    Rule rows are ``(PATTERN TEMPLATE)``. Each pattern is lowered at runtime by
    ``parse-match-pattern`` (see :func:`koskari.matcher.parse_explicit_rule_pattern`).

    Lowers to ``(build-syntax NAME-STR (parse-match-pattern (quote-syntax PATTERN))
    (quote-syntax TEMPLATE) ...)`` for recompilation. The built transformer is
    the value of that expression; use ``(compile/define NAME ...)`` to bind it.
    Pattern parsing runs when that expression is evaluated.
    """

    namesym = name.datum

    parts = []
    for res in rules:
        raw_rule = res["rule"].datum
        pattern_src, template_expr = _syntax_rules_row_pattern_and_template(
            raw_rule)
        parts.append(_emit_parse_match_pattern(pattern_src))
        parts.append(cons(_sym_quote_syntax, template_expr, nil))

    return cons(symbol("build-syntax"), str(namesym), *parts, nil)


_MACRO_MATCH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("name"), _MATCH_RULE_ROWS),
]


@builtin("compile/macro-match")
@special("compile/macro-match", patterns=_MACRO_MATCH_PATTERNS,
         unpack="kwargs")
def special_compile_macro_match(compiler, called_by, name, rules):
    """
    Compile (compile/macro-match NAME RULE ...).

    Rule rows are ``(PATTERN BODY...)``; each pattern is passed to
    :func:`koskari.matcher.parse_explicit_rule_pattern` at compile time only to
    derive matcher slot order for generated formals.

    Lowers to ``(build-macro NAME-STR (parse-match-pattern (quote-syntax PATTERN))
    (lambda FORMALS . BODY) ...)`` for recompilation. The built
    transformer is the value of that expression; use ``(compile/define NAME ...)``
    to bind it.
    """

    namesym = name.datum

    parts = []

    for sub_res in rules:
        pattern_src = sub_res["pattern"].datum
        body_src = sub_res["body"].datum

        pattern_matcher = parse_explicit_rule_pattern(pattern_src)

        parts.append(_emit_parse_match_pattern(pattern_src))
        parts.append(cons(symbol("lambda"),
                          build_proper(*slot_formals(pattern_matcher)),
                          body_src))

    return cons(symbol("build-macro"), str(namesym), *parts, nil)


_SPECIAL_RULES_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("name"),
        BIND("literals"),
        _SPECIAL_RULE_ROWS),
]


@builtin("compile/special-rules")
@special("compile/special-rules", patterns=_SPECIAL_RULES_PATTERNS,
         unpack="kwargs")
def special_compile_special_rules(compiler, called_by, name, literals, rules):
    """
    Compile (compile/special-rules NAME LITERALS RULES...).

    Each rule pattern column is ``(compiler-formal pattern)``: the first
    subform names the compiler parameter (any symbol), the second is the
    nested RULE pattern. PATTERN is
    lowered with :func:`parse_syntax_rules_pattern`. Lowers to a
    ``(build-special NAME-STR (parse-rule-pattern (quote-syntax INNER-PATTERN)
    (quote-syntax LITERALS-SET)) (lambda ...) ...)`` call form for
    recompilation. The built transformer is the value of that expression; use
    ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum

    literal_set = set(unpack(literals.datum))
    literal_set.add(namesym)

    parts = []
    for res in rules:
        compiler_formal = res["compiler"].datum
        inner = res["pattern"].datum
        body_forms = res["body"].datum

        vars_ordered = collect_pattern_variable_symbols(inner, literal_set)

        parts.append(_emit_parse_rule_pattern(inner, literal_set))
        parts.append(cons(symbol("lambda"),
                          build_proper(compiler_formal, *vars_ordered),
                          body_forms))

    return cons(symbol("build-special"), str(namesym), *parts, nil)


_SPECIAL_MATCH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("name"), _SPECIAL_MATCH_RULE_ROWS),
]


@builtin("compile/special-match")
@special("compile/special-match", patterns=_SPECIAL_MATCH_PATTERNS,
         unpack="kwargs")
def special_compile_special_match(compiler, called_by, name, rules):
    """
    Compile (compile/special-match NAME RULE ...).

    Each rule pattern column is ``(compiler-formal pattern)``: the first
    subform names the compiler parameter (any symbol), the second is the
    nested MATCH pattern for parse_explicit_rule_pattern.

    Lowers to a ``(build-special NAME-STR (parse-match-pattern
    (quote-syntax INNER-PATTERN)) (lambda ...) ...)`` call form for
    recompilation (same shape as ``compile/macro-match``). The built transformer
    is the value of that expression; use ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum

    parts = []

    for res in rules:
        compiler_formal = res["compiler"].datum
        inner = res["pattern"].datum
        body_src = res["body"].datum

        if is_symbol(inner):
            inner = cons(cons(symbol("REST"), inner, nil), nil)

        pattern_matcher = parse_explicit_rule_pattern(inner)

        parts.append(_emit_parse_match_pattern(inner))
        parts.append(cons(symbol("lambda"),
                          build_proper(compiler_formal,
                                       *slot_formals(pattern_matcher)),
                          body_src))

    return cons(symbol("build-special"), str(namesym), *parts, nil)


def slot_formals(pattern_matcher):
    """
    The formals an explicit-pattern arm takes for its captures: each
    slot once, in slot-table order, as a keyword formal defaulting to
    None. A match result is sparse, so a slot behind TAIL or an
    unmatched ONEOF branch arrives as None, and a slot the table lists
    once per branch (BINDINGS) is one formal

    :param pattern_matcher: the lowered rule pattern
    :return: the formals list items, keyword and default alternating
    """

    out = []
    for name in dict.fromkeys(pattern_slot_names(pattern_matcher)):
        out.append(keyword(name))
        out.append(None)
    return out


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

        # the spine's elements, then a dotted tail, which the lowerer
        # binds as a pattern variable when it is a symbol
        items = []
        cell = expr
        while is_pair(cell) and not is_nil(cell):
            items.append(cell.car)
            cell = cell.cdr
        i = 0
        while i < len(items):
            if i + 1 < len(items) and items[i + 1] is Ellipsis:
                walk(items[i])
                i += 2
            else:
                walk(items[i])
                i += 1
        if not is_nil(cell) and is_symbol(cell):
            walk(cell)

    walk(pattern_expr)
    return seen


def parse_macro_rules_rows(name, literals_expr, raw_rules):
    """
    Parse macro-rules literals and rules tail into matchers and body datums

    No ``lambda`` or ``build-macro`` source is emitted here; compile
    specials lower that from the returned rows.

    :param name: macro name symbol (included in literals)
    :param literals_expr: literals list expression (same position as in
        ``(macro-rules LITERALS RULE ...)``)
    :param raw_rules: the rule row datums, in source order
    :return: ``(literals, rows)`` where each row is ``(pattern_matcher,
        pattern_src, formals_tuple, list of body forms)``
    :raises SpecialError: when the rules are not valid macro-rules rows
    """

    literals = set(unpack(literals_expr))
    literals.add(name)

    rows = []
    for raw_rule in raw_rules:
        if not is_pair(raw_rule):
            raise SpecialError("invalid macro-rules rule", raw_rule)

        if not is_proper(raw_rule):
            raise SpecialError("macro-rules rule must be a proper list", raw_rule)

        pattern_src, body_forms = raw_rule
        if not body_forms:
            raise SpecialError("macro-rules rule requires a body", raw_rule)

        pattern = parse_syntax_rules_pattern(pattern_src, literals, raw_rule)
        varsyms = collect_pattern_variable_symbols(pattern_src, literals)
        rows.append((pattern, pattern_src, tuple(varsyms), body_forms))

    return literals, tuple(rows)


# The end.
