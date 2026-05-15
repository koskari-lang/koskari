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
tests.syntax.datum_syntax_helpers

Test-only construction of ``Syntax`` from pattern/template rows (RULE or
MATCH dialect) and from procedural macro/special ``(pattern, procedure)``
rows, using the same lowering as ``define-syntax`` compile surfaces.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from koskari.lib import nil
from koskari.matcher import parse_explicit_rule_pattern
from koskari.syntax import (
    MacroTransform,
    SpecialTransform,
    Syntax,
    SyntaxTransform,
    parse_syntax_rules_pattern,
)


def syntax_from_rule_datums(name, literals, rules, source_ctx=None):
    """
    Build ``Syntax`` from RULE-dialect pattern datums and templates

    :param name: dispatch symbol or string
    :param literals: iterable of literal symbols for ``parse_syntax_rules_pattern``
    :param rules: iterable of ``(pattern_datum, template_datum)`` rows
    :param source_ctx: context for pattern parse errors
    :return: ``Syntax`` instance
    :raises ValueError: when ``rules`` is empty or a row has wrong shape
    """

    rows = tuple(rules)
    if not rows:
        raise ValueError("syntax_from_rule_datums requires at least one rule")

    lit = frozenset(literals) if literals is not None else frozenset()
    transforms = []
    for row in rows:
        if not isinstance(row, (list, tuple)):
            raise TypeError(
                "syntax-rules row must be a sequence, got %r" % (row,))
        seq = tuple(row)
        if len(seq) != 2:
            raise ValueError(
                "syntax-rules row must be (pattern, template), "
                "got length %s" % (len(seq),))
        pat_d, tmpl = seq
        ptree = parse_syntax_rules_pattern(pat_d, lit, source_ctx)
        transforms.append(SyntaxTransform(ptree, nil, tmpl, pat_d))

    return Syntax(name, tuple(transforms))


def syntax_from_match_datums(name, rules, source_ctx=None):
    """
    Build ``Syntax`` from MATCH-dialect pattern datums and templates

    :param name: dispatch symbol or string
    :param rules: iterable of ``(pattern_datum, template_datum)`` rows
    :param source_ctx: context for pattern parse errors
    :return: ``Syntax`` instance
    :raises ValueError: when ``rules`` is empty or a row has wrong shape
    """

    rows = tuple(rules)
    if not rows:
        raise ValueError("syntax_from_match_datums requires at least one rule")

    transforms = []
    for row in rows:
        if not isinstance(row, (list, tuple)):
            raise TypeError(
                "syntax-match row must be a sequence, got %r" % (row,))
        seq = tuple(row)
        if len(seq) != 2:
            raise ValueError(
                "syntax-match row must be (pattern, template), "
                "got length %s" % (len(seq),))
        pat_d, tmpl = seq
        ptree = parse_explicit_rule_pattern(pat_d, source_ctx)
        transforms.append(SyntaxTransform(ptree, nil, tmpl, pat_d))

    return Syntax(name, tuple(transforms))


def _normalize_pattern_procedure_row(row):
    """
    Split one macro-rules or special-rules row into pattern datum and Python
    procedure
    """

    if not isinstance(row, (list, tuple)):
        raise TypeError(
            "macro/special rule row must be a sequence, got %r" % (row,))
    seq = tuple(row)
    if len(seq) != 2:
        raise ValueError(
            "macro/special rule row must be (pattern, procedure), "
            "got length %s" % (len(seq),))
    pat_d, proc = seq
    if proc is None or not callable(proc):
        raise TypeError(
            "macro/special rule procedure must be callable, got %r"
            % (proc,))
    return pat_d, proc


def macro_from_rule_rows(name, literals, rule_rows, source_ctx=None):
    """
    Build ``Syntax`` of ``MacroTransform`` arms from RULE-dialect patterns
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("macro_from_rule_rows requires at least one rule")

    lit = frozenset(literals) if literals is not None else frozenset()
    transforms = []
    for row in rows:
        pat_d, proc = _normalize_pattern_procedure_row(row)
        ptree = parse_syntax_rules_pattern(pat_d, lit, source_ctx)
        transforms.append(MacroTransform(ptree, proc))

    return Syntax(name, tuple(transforms))


def macro_from_match_rows(name, rule_rows, source_ctx=None):
    """
    Build ``Syntax`` of ``MacroTransform`` arms from MATCH-dialect patterns
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("macro_from_match_rows requires at least one rule")

    transforms = []
    for row in rows:
        pat_d, proc = _normalize_pattern_procedure_row(row)
        ptree = parse_explicit_rule_pattern(pat_d, source_ctx)
        transforms.append(MacroTransform(ptree, proc))

    return Syntax(name, tuple(transforms))


def special_from_rule_rows(name, literals, rule_rows, source_ctx=None):
    """
    Build ``Syntax`` of ``SpecialTransform`` arms from RULE-dialect patterns
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("special_from_rule_rows requires at least one rule")

    lit = frozenset(literals) if literals is not None else frozenset()
    transforms = []
    for row in rows:
        pat_d, proc = _normalize_pattern_procedure_row(row)
        ptree = parse_syntax_rules_pattern(pat_d, lit, source_ctx)
        transforms.append(SpecialTransform(ptree, proc))

    return Syntax(name, tuple(transforms))


def special_from_match_rows(name, rule_rows, source_ctx=None):
    """
    Build ``Syntax`` of ``SpecialTransform`` arms from MATCH-dialect patterns
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("special_from_match_rows requires at least one rule")

    transforms = []
    for row in rows:
        pat_d, proc = _normalize_pattern_procedure_row(row)
        ptree = parse_explicit_rule_pattern(pat_d, source_ctx)
        transforms.append(SpecialTransform(ptree, proc))

    return Syntax(name, tuple(transforms))


# The end.
