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
koskari.compile.macro_rules

macro-rules transformers: pattern + compile-time lambda body, Syntax-valued
arguments from matcher results.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
"""

from dataclasses import dataclass
from typing import Optional

from ..exceptions import CompilerError
from ..lib import FunctionSpec, OpCode, cons, nil, symbol, demand_call
from ..matcher import (
    BIND,
    LITERAL_SYM,
    MATCH,
    Matcher,
    REPEAT,
    REST,
    from_tuple as matcher_from_tuple,
    match,
)
from ..matcher import match_dict_to_syntax_kwargs
from ..types import get_position, is_pair, is_proper, build_proper
from .syntax import (
    _deserialize_form,
    _serialize_form,
    _to_list,
    collect_pattern_variable_symbols,
    parse_rule_pattern,
)


_tag_macro_rules = symbol("syntax/macro-rules")


def _maybe_unwrap_repeat_macro_kwarg(val, formal_sym):
    """
    If val is a list of one-key dicts mapping formal_sym to Syntax, return the
    list of Syntax values; otherwise return val unchanged.
    """

    if not isinstance(val, list) or not val:
        return val

    for row in val:
        if not isinstance(row, dict) or len(row) != 1 or formal_sym not in row:
            return val

    return [row[formal_sym] for row in val]


_MACRO_RULES_HEAD_PATTERN = MATCH(
    LITERAL_SYM("macro-rules"),
    MATCH(BIND("literals"), REST("rules")))

_MACRO_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))


def _extract_function_spec(fragment):
    """
    Return FunctionSpec emitted by ``special_lambda`` on the fragment.
    """

    fspec = None
    for op, arg in fragment.instructions:
        if op == OpCode.KOS_OP_CONST and isinstance(arg, FunctionSpec):
            fspec = arg

    if fspec is None:
        raise CompilerError("internal: missing FunctionSpec in macro lambda")

    return fspec


def _function_from_spec(compiler, fspec):
    """
    Build a Koskari Function from FunctionSpec and the compiler module.
    """

    from ..lib import Function

    return Function(fspec.code, vars(compiler.module), fspec.formals_spec)


def _compile_macro_lambda(compiler, pattern_src, body_forms, literals, raw_rule):
    """
    Lower one macro-rules clause to pattern matcher, formals, and FunctionSpec.
    """

    if not body_forms:
        raise CompilerError("macro-rules rule requires a body", raw_rule)

    pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
    vars_ordered = collect_pattern_variable_symbols(pattern_src, literals)
    formals = build_proper(*vars_ordered)

    if len(body_forms) == 1:
        body_expr = body_forms[0]
    else:
        body_expr = cons(
            symbol("compile/begin"),
            build_proper(*body_forms))

    lambda_src = cons(
        symbol("compile/lambda"),
        cons(formals, cons(body_expr, nil)))

    from .helpers import SPECIALS

    lam_special = SPECIALS[symbol("compile/lambda")]
    frag = lam_special.compile_impl(compiler, lambda_src, False)
    fspec = _extract_function_spec(frag)

    return pattern_matcher, tuple(vars_ordered), lambda_src, fspec


def build_macro_rules_transformer(compiler, name, spec):
    """
    Parse (macro-rules (LIT ...) ((PATTERN BODY ...) ...)) into transformer.
    """

    head_m = match(spec, _MACRO_RULES_HEAD_PATTERN)
    if head_m is None:
        raise CompilerError("define-syntax requires macro-rules form", spec)

    literals_expr = head_m["literals"].source
    rules_expr = head_m["rules"].source

    literals = frozenset(_to_list(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _MACRO_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise CompilerError("macro-rules requires at least one rule", spec)

    rules = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise CompilerError("invalid macro-rules rule", raw_rule)

        if not is_proper(raw_rule):
            raise CompilerError("macro-rules rule must be a proper list", raw_rule)

        items = _to_list(raw_rule)
        if len(items) < 2:
            raise CompilerError(
                "macro-rules rule needs pattern and at least one body form",
                raw_rule)

        pattern_src = items[0]
        body_forms = items[1:]
        pm, formals, lam_src, fspec = _compile_macro_lambda(
            compiler, pattern_src, body_forms, literals, raw_rule)
        rules.append(MacroRule(pm, formals, lam_src, fspec))

    return MacroRulesTransformer(name, literals, rules)


@dataclass
class MacroRule:
    """
    One macro-rules clause: matcher, ordered formals, serialized lambda, and
    optional cached FunctionSpec after compile.
    """

    pattern_matcher: Matcher
    formals: tuple
    lambda_source: object
    function_spec: Optional[FunctionSpec] = None


    def to_tuple(self):
        return (
            self.pattern_matcher.to_tuple(),
            tuple(_serialize_form(x) for x in self.formals),
            _serialize_form(self.lambda_source))


    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 3:
            raise ValueError("invalid macro rule tuple")

        pm = matcher_from_tuple(tup[0])
        formals = tuple(_deserialize_form(x) for x in tup[1])
        lam = _deserialize_form(tup[2])

        return cls(pm, formals, lam, None)


    def expand(self, compiler, source_obj, literals):
        """
        Match, build Syntax kwargs, invoke expand-time lambda.
        """

        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        pos_fb = get_position(source_obj) if is_pair(source_obj) else None
        kwargs = match_dict_to_syntax_kwargs(raw, position_fallback=pos_fb)
        if kwargs is None:
            return None

        for s in self.formals:
            if s not in kwargs:
                return None

        fspec = self.function_spec
        if fspec is None:
            from .helpers import SPECIALS

            lam_special = SPECIALS[symbol("compile/lambda")]
            frag = lam_special.compile_impl(
                compiler, self.lambda_source, False)
            fspec = _extract_function_spec(frag)

        fn = _function_from_spec(compiler, fspec)
        args = [
            _maybe_unwrap_repeat_macro_kwarg(kwargs[s], s) for s in self.formals]

        return demand_call(fn, *args)


@dataclass(frozen=True)
class MacroRulesSegment:
    """
    Literals plus ordered macro-rules for one segment.
    """

    literals: frozenset
    rules: tuple


class MacroRulesTransformer:
    """
    Expand-time transformer for macro-rules (procedural macro).
    """

    def __init__(self, name, literals, rules):
        self.name = name
        self.segments = (
            MacroRulesSegment(frozenset(literals), tuple(rules)),
        )


    @classmethod
    def from_segments(cls, name, segments):
        inst = cls.__new__(cls)
        inst.name = name
        inst.segments = tuple(segments)
        return inst


    def to_tuple(self):
        if len(self.segments) != 1:
            raise ValueError(
                "MacroRulesTransformer.to_tuple requires exactly one segment")

        seg = self.segments[0]
        return (
            _tag_macro_rules,
            _serialize_form(self.name),
            tuple(_serialize_form(x) for x in seg.literals),
            tuple(r.to_tuple() for r in seg.rules))


    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 4 or tup[0] is not _tag_macro_rules:
            raise ValueError("invalid macro-rules tuple")

        name = _deserialize_form(tup[1])
        literals = frozenset(_deserialize_form(x) for x in tup[2])
        rules = tuple(MacroRule.from_tuple(rt) for rt in tup[3])

        return cls.from_segments(name, (MacroRulesSegment(literals, rules),))


    def compile_impl(self, compiler, source_obj, tc=False):
        for segment in self.segments:
            for rule in segment.rules:
                expanded = rule.expand(compiler, source_obj, segment.literals)
                if expanded is not None:
                    return expanded

        raise CompilerError("no matching macro-rules rule", source_obj)


def build_macro_syntax(name, tagged_tuple):
    """
    Runtime helper: deserialize a tagged macro-rules tuple.
    """

    transformer = MacroRulesTransformer.from_tuple(tagged_tuple)
    if name is not None:
        transformer.name = name

    return transformer


# The end.
