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
from ...hygiene import extern_atoms, function_constant
from ...lib import FormalsSpec, OpCode, cons, keyword, nil, symbol
from ...match_code import SUBJECT, FragmentTarget, lower_matcher
from ...matcher import (
    BIND, BODY, CALLEDBY, LIST, MATCH, REPEAT, SYMBOL,
    VALUE, MatchCompiler, parse_explicit_rule_pattern, pattern_slot_names,
    repeat_capture)
from ...syntax import parse_rule_pattern, parse_syntax_rules_pattern, special
from ...types import (
    build_proper, is_pair, is_proper, is_symbol, unpack)


_sym_quote_syntax = symbol("quote-syntax")


def matcher_function(compiler, parent, pattern, pattern_src, name):
    """
    The matcher of a rule as a function constant of the defining
    module: the pattern lowered through the match compiler's inline
    target into a lambda body whose one formal is the subject, every
    extern the pattern names requested from the module through the
    closure apparatus under the atom the pattern wrote, and the body
    completed as any lambda's is, marks included

    :param compiler: the compiler
    :param parent: the fragment the CONST row is emitted into
    :param pattern: the lowered pattern
    :param pattern_src: the pattern's source form
    :param name: the function's name
    :return: (function constant, frozen tuple)
    """

    frozen = MatchCompiler().lower(pattern)
    atoms = extern_atoms(pattern_src, frozen[2])
    body = compiler.fragment(name)
    body.declare_var(symbol(SUBJECT))
    for extern in frozen[2]:
        body.request_var(atoms.get(extern) or symbol(extern))
    with compiler.resolution_scope(body, function=True):
        lower_matcher(
            FragmentTarget(compiler, body), frozen, atoms, pattern_src)
    constant = function_constant(
        compiler, parent, body, name, FormalsSpec((SUBJECT,)))
    return constant, frozen


def emit_matcher_row(compiler, frag, pattern, pattern_src, name):
    """
    Push the three leading arguments of a build row: the matcher
    function built in the module, the names tuple (the slot names and
    the repeat variables as pairs), and the quoted pattern source for
    help text and errors

    :return: the number of arguments pushed
    """

    constant, frozen = matcher_function(
        compiler, frag, pattern, pattern_src, name)
    frag.instruction(OpCode.CONST, constant)
    frag.instruction(OpCode.BUILD_FUNCTION, ((), (), 0, 0))
    repeat_variables = getattr(pattern, "repeat_variables", None) or {}
    repeat_items = tuple(
        (slot, tuple(names)) for slot, names in repeat_variables.items())
    frag.instruction(OpCode.CONST, (tuple(frozen[1]), repeat_items))
    frag.merge(compiler.full_compile(
        cons(_sym_quote_syntax, pattern_src, nil)))
    return 3


def emit_build(compiler, called_by, builtin_name, name_str, rows):
    """
    The call of a build builtin: the transformer name, then each row's
    arguments, then the call

    :param rows: functions of the fragment, each pushing a row's
        arguments and returning their count
    """

    frag = compiler.fragment(called_by)
    frag.instruction(OpCode.CONST, name_str)
    count = 1
    for row in rows:
        count += row(frag)
    sym = symbol(builtin_name)
    frag.request_var(sym)
    frag.instruction(OpCode.CALL_VAR, (sym, count, 0, 0))
    return frag


def quoted(compiler, form):
    """
    The fragment of (quote-syntax FORM)
    """

    return compiler.full_compile(cons(_sym_quote_syntax, form, nil))


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

    Each rule's pattern is parsed here and lowered to a matcher
    function, a lambda of this module whose externs close over the
    module's bindings; the form lowers to a call of ``build-syntax``
    with, per rule, that function, its names, the quoted pattern and
    the quoted template. ``LITERALS`` is the literals list form from the
    source; the macro name is not a literal. The head of each pattern
    lowers to a CALLEDBY capture under its own name, binding the symbol
    the macro was invoked as, so a template may use it. The built
    transformer is the value of the expression; use
    ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum
    literals_expr = literals.datum

    rows = []
    for res in rules:
        raw_rule = res["rule"].datum
        pattern_src, template_expr = _syntax_rules_row_pattern_and_template(
            raw_rule)
        pattern = parse_rule_pattern(pattern_src, literals_expr, raw_rule)

        def row(frag, pattern=pattern, pattern_src=pattern_src,
                template_expr=template_expr):
            count = emit_matcher_row(
                compiler, frag, pattern, pattern_src, str(namesym))
            frag.merge(quoted(compiler, template_expr))
            return count + 1

        rows.append(row)

    return emit_build(compiler, called_by, "build-syntax", str(namesym), rows)


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

    Lowers to ``(build-bare-syntax NAME-STR SLOT-STR (quote-syntax
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

    Rules are ``(PATTERN BODY...)``; the patterns are parsed here and
    lowered to matcher functions of this module. Lowers to a call of
    ``build-macro`` with, per rule, the function, its names, the quoted
    pattern and ``(lambda FORMALS . BODY)``, the formals being the
    pattern's variables. The built transformer is the value of the
    expression; use ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum
    literals_expr = literals.datum
    raw_rules = [res["rule"].datum for res in rules]
    _literal_set, parsed = parse_macro_rules_rows(
        namesym, literals_expr, raw_rules)

    rows = []
    for pattern, pattern_src, formals, body_forms in parsed:
        lambda_form = cons(symbol("lambda"), build_proper(*formals), body_forms)

        def row(frag, pattern=pattern, pattern_src=pattern_src,
                lambda_form=lambda_form):
            count = emit_matcher_row(
                compiler, frag, pattern, pattern_src, str(namesym))
            frag.merge(compiler.full_compile(lambda_form))
            return count + 1

        rows.append(row)

    return emit_build(compiler, called_by, "build-macro", str(namesym), rows)


_SYNTAX_MATCH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("name"), _RULE_ROWS),
]


@builtin("compile/syntax-match")
@special("compile/syntax-match", patterns=_SYNTAX_MATCH_PATTERNS,
         unpack="kwargs")
def special_compile_syntax_match(compiler, called_by, name, rules):
    """
    Compile (compile/syntax-match NAME RULE ...).

    Rule rows are ``(PATTERN TEMPLATE)``; each pattern is lowered here
    through :func:`koskari.matcher.parse_explicit_rule_pattern` to a
    matcher function of this module. Lowers to a call of
    ``build-syntax`` with, per rule, the function, its names, the quoted
    pattern and the quoted template. The built transformer is the value
    of the expression; use ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum
    rows = []
    for res in rules:
        raw_rule = res["rule"].datum
        pattern_src, template_expr = _syntax_rules_row_pattern_and_template(
            raw_rule)
        pattern = parse_explicit_rule_pattern(pattern_src)

        def row(frag, pattern=pattern, pattern_src=pattern_src,
                template_expr=template_expr):
            count = emit_matcher_row(
                compiler, frag, pattern, pattern_src, str(namesym))
            frag.merge(quoted(compiler, template_expr))
            return count + 1

        rows.append(row)

    return emit_build(compiler, called_by, "build-syntax", str(namesym), rows)


_MACRO_MATCH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("name"), _MATCH_RULE_ROWS),
]


@builtin("compile/macro-match")
@special("compile/macro-match", patterns=_MACRO_MATCH_PATTERNS,
         unpack="kwargs")
def special_compile_macro_match(compiler, called_by, name, rules):
    """
    Compile (compile/macro-match NAME RULE ...).

    Rule rows are ``(PATTERN BODY...)``; each pattern is lowered here
    through :func:`koskari.matcher.parse_explicit_rule_pattern` to a
    matcher function of this module, and its slot order gives the
    generated formals. Lowers to a call of ``build-macro`` with, per
    rule, the function, its names, the quoted pattern and
    ``(lambda FORMALS . BODY)``. The built transformer is the value of
    the expression; use ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum
    rows = []
    for sub_res in rules:
        pattern_src = sub_res["pattern"].datum
        body_src = sub_res["body"].datum
        pattern = parse_explicit_rule_pattern(pattern_src)
        lambda_form = cons(
            symbol("lambda"), build_proper(*slot_formals(pattern)), body_src)

        def row(frag, pattern=pattern, pattern_src=pattern_src,
                lambda_form=lambda_form):
            count = emit_matcher_row(
                compiler, frag, pattern, pattern_src, str(namesym))
            frag.merge(compiler.full_compile(lambda_form))
            return count + 1

        rows.append(row)

    return emit_build(compiler, called_by, "build-macro", str(namesym), rows)


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
    subform names the compiler parameter (any symbol), the second is
    the nested RULE pattern, lowered here with
    :func:`parse_syntax_rules_pattern` to a matcher function of this
    module. Lowers to a call of ``build-special`` with, per rule, the
    function, its names, the quoted pattern and ``(lambda ...)``. The
    built transformer is the value of the expression; use
    ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum
    literal_set = set(unpack(literals.datum))
    literal_set.add(namesym)

    rows = []
    for res in rules:
        compiler_formal = res["compiler"].datum
        inner = res["pattern"].datum
        body_forms = res["body"].datum
        variables = {}
        pattern = parse_syntax_rules_pattern(
            inner, literal_set, inner, variables)
        lambda_form = cons(
            symbol("lambda"),
            build_proper(compiler_formal, *variables.values()),
            body_forms)

        def row(frag, pattern=pattern, inner=inner, lambda_form=lambda_form):
            count = emit_matcher_row(
                compiler, frag, pattern, inner, str(namesym))
            frag.merge(compiler.full_compile(lambda_form))
            return count + 1

        rows.append(row)

    return emit_build(
        compiler, called_by, "build-special", str(namesym), rows)


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
    subform names the compiler parameter (any symbol), the second is
    the nested MATCH pattern for parse_explicit_rule_pattern, lowered
    here to a matcher function of this module. Lowers to a call of
    ``build-special`` with, per rule, the function, its names, the
    quoted pattern and ``(lambda ...)``, as ``compile/macro-match``
    does. The built transformer is the value of the expression; use
    ``(compile/define NAME ...)`` to bind it.
    """

    namesym = name.datum
    rows = []
    for res in rules:
        compiler_formal = res["compiler"].datum
        inner = res["pattern"].datum
        body_src = res["body"].datum
        if is_symbol(inner):
            inner = cons(cons(symbol("REST"), inner, nil), nil)
        pattern = parse_explicit_rule_pattern(inner)
        lambda_form = cons(
            symbol("lambda"),
            build_proper(compiler_formal, *slot_formals(pattern)),
            body_src)

        def row(frag, pattern=pattern, inner=inner, lambda_form=lambda_form):
            count = emit_matcher_row(
                compiler, frag, pattern, inner, str(namesym))
            frag.merge(compiler.full_compile(lambda_form))
            return count + 1

        rows.append(row)

    return emit_build(
        compiler, called_by, "build-special", str(namesym), rows)


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

        variables = {}
        pattern = parse_syntax_rules_pattern(
            pattern_src, literals, raw_rule, variables)
        rows.append(
            (pattern, pattern_src, tuple(variables.values()), body_forms))

    return literals, tuple(rows)


# The end.
