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
koskari.syntax

Syntax-domain API: Syntax boxing, syntax-rules / macro-rules pattern
compilation, transformer models, and runtime tagged-tuple builders.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from dataclasses import dataclass
from typing import Optional

from .exceptions import CompilerError, SpecialError
from .lib import (FunctionSpec, OpCode, car, cdr, cons, demand_call, gensym,
                  nil, symbol)
from .matcher import (BIND, LIST, LITERAL, LITERAL_SYM, MATCH, REPEAT, REST,
                      VALUE, BindingResult, BindingsResult, FormalsResult,
                      Matcher, MatchResult, RepeatResult,
                      extract_binder_symbols)
from .matcher import from_tuple as matcher_from_tuple
from .matcher import is_binder_spec, is_scope_region_spec, match
from .types import (build_proper, get_position, is_pair, is_proper,
                    is_symbol, unpack)

__all__ = (
    'parse_rule_pattern',
    'syntax_pattern_to_matcher',
    'collect_pattern_variable_symbols',
    'SyntaxRule',
    'SyntaxRulesSegment',
    'SyntaxRulesTransformer',
    'build_syntax',
    'build_syntax_rules_transformer',
    'MacroRule',
    'MacroRulesSegment',
    'MacroRulesTransformer',
    'build_macro_syntax',
    'build_macro_rules_transformer',
)


_ellipsis = symbol("...")
_symbol_match = symbol("match")
_symbol_rule = symbol("rule")


def parse_explicit_rule_pattern(inner, literals, source_ctx):
    """
    Placeholder for explicit ``rule`` pattern mode (decorated matcher atoms).

    :param inner: Single form inside ``(rule ...)``
    :param literals: Literal symbols for this syntax-rules segment
    :param source_ctx: Original rule form for error reporting
    :raises CompilerError: always until ``rule`` parsing is implemented
    """

    raise CompilerError("rule pattern mode not implemented yet", source_ctx)


def parse_rule_pattern(pattern_expr, literals, source_ctx):
    """
    Dispatch syntax-rules pattern source into ``match`` vs ``rule`` parsers,
    then lower to matcher IR via ``syntax_pattern_to_matcher`` (except ``rule``,
    which is not implemented yet).
    """

    if is_pair(pattern_expr):
        head = car(pattern_expr)
        if head == _symbol_match:
            rest = cdr(pattern_expr)
            if not is_proper(rest) or cdr(rest) is not nil:
                raise CompilerError(
                    "match pattern must have exactly one form", pattern_expr)

            return syntax_pattern_to_matcher(car(rest), literals, source_ctx)

        if head == _symbol_rule:
            rest = cdr(pattern_expr)
            if not is_proper(rest) or cdr(rest) is not nil:
                raise CompilerError(
                    "rule pattern must have exactly one form", pattern_expr)

            return parse_explicit_rule_pattern(car(rest), literals, source_ctx)

    return syntax_pattern_to_matcher(pattern_expr, literals, source_ctx)


def syntax_pattern_to_matcher(pattern_expr, literals, source_ctx):
    """
    Lower a syntax-rules pattern cons tree to matcher IR.
    """

    if _is_syntax_ellipsis(pattern_expr):
        raise CompilerError("invalid ellipsis placement in pattern", source_ctx)

    if is_symbol(pattern_expr):
        if pattern_expr in literals:
            return MATCH(LITERAL(pattern_expr))
        return MATCH(REST(pattern_expr))

    if not is_pair(pattern_expr):
        return MATCH(LITERAL(pattern_expr))

    return MATCH(*_syntax_pattern_specs(
        _to_list(pattern_expr), literals, source_ctx))


def _syntax_elem_matcher(pat, literals, source_ctx):
    if _is_syntax_ellipsis(pat):
        raise CompilerError("invalid ellipsis placement in pattern", source_ctx)

    if is_symbol(pat):
        if pat in literals:
            return LITERAL(pat)
        return VALUE(pat)

    if not is_pair(pat):
        return LITERAL(pat)

    return LIST(*_syntax_pattern_specs(
        _to_list(pat), literals, source_ctx))


def _syntax_pattern_specs(p_items, literals, source_ctx):
    specs = []
    i = 0
    while i < len(p_items):
        if i + 1 < len(p_items) and _is_syntax_ellipsis(p_items[i + 1]):
            pat = p_items[i]
            inner = _syntax_elem_matcher(pat, literals, source_ctx)
            slot = gensym('_repeat')
            specs.append(REPEAT(slot, inner, min_count=0))
            i += 2
            continue

        specs.append(_syntax_elem_matcher(p_items[i], literals, source_ctx))
        i += 1

    return specs


def _is_syntax_ellipsis(obj):
    return obj is _ellipsis or obj is Ellipsis


def collect_pattern_variable_symbols(pattern_expr, literals):
    """
    Pattern variables in pre-order, excluding literals and ellipsis markers.
    Each symbol appears once (first occurrence). Used for macro-rules formals.
    """

    seen = []

    def walk(expr):
        if _is_syntax_ellipsis(expr):
            return

        if is_symbol(expr):
            if expr in literals:
                return

            if expr not in seen:
                seen.append(expr)

            return

        if not is_pair(expr):
            return

        items = _to_list(expr)
        i = 0
        while i < len(items):
            if i + 1 < len(items) and _is_syntax_ellipsis(items[i + 1]):
                walk(items[i])
                i += 2
            else:
                walk(items[i])
                i += 1

    walk(pattern_expr)
    return seen


_tag_syntax_rules = symbol("syntax/rules")
_tag_macro_rules = symbol("syntax/macro-rules")
_tag_list = symbol("syntax/list")
_tag_cons = symbol("syntax/cons")

_SYNTAX_RULES_HEAD_PATTERN = MATCH(
    LITERAL_SYM("syntax-rules"),
    MATCH(BIND("literals"), REST("rules")))
_SYNTAX_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))

_MACRO_RULES_HEAD_PATTERN = MATCH(
    LITERAL_SYM("macro-rules"),
    MATCH(BIND("literals"), REST("rules")))
_MACRO_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))


def _to_list(value):
    if value is nil:
        return []
    if not is_proper(value):
        raise CompilerError("expected proper list", value)
    return list(unpack(value))


def _serialize_form(form):
    if form is nil:
        return nil

    if is_pair(form):
        if is_proper(form):
            return (_tag_list,) + tuple(_serialize_form(x) for x in unpack(form))
        return (_tag_cons, _serialize_form(car(form)), _serialize_form(cdr(form)))

    if isinstance(form, tuple):
        return tuple(_serialize_form(x) for x in form)

    if isinstance(form, frozenset):
        return frozenset(_serialize_form(x) for x in form)

    return form


def _deserialize_form(payload):
    if payload is nil:
        return nil

    if isinstance(payload, tuple) and payload:
        tag = payload[0]

        if tag is _tag_list:
            return build_proper(*(_deserialize_form(x) for x in payload[1:]))

        if tag is _tag_cons:
            if len(payload) != 3:
                raise ValueError("invalid cons payload")
            return cons(_deserialize_form(payload[1]), _deserialize_form(payload[2]))

        return tuple(_deserialize_form(x) for x in payload)

    if isinstance(payload, frozenset):
        return frozenset(_deserialize_form(x) for x in payload)

    return payload


def _match_dict_to_syntax_kwargs(raw, position_fallback=None):
    if not raw:
        return {}

    out = {}
    for k, v in raw.items():
        boxed = v.box(position_fallback)
        if boxed is None:
            return None

        out[symbol(k)] = boxed

    return out


def _merge_bindings(dst, src):
    for k, v in src.items():
        if k in dst:
            cur = dst[k]
            if isinstance(cur, list) or isinstance(v, list):
                if not isinstance(cur, list) or not isinstance(v, list):
                    return False
                if cur != v:
                    return False
            elif cur != v:
                return False
        else:
            dst[k] = v

    return True


def _syntax_match_result_to_bindings(raw):
    if not raw:
        return {}

    out = {}

    for k, v in raw.items():
        boxed = v.box()
        if boxed is None:
            return None

        if isinstance(boxed, list):
            merged = {}
            for item in boxed:
                for rk, val in item.items():
                    merged.setdefault(rk, []).append(val.datum)

            if not _merge_bindings(out, merged):
                return None

        else:
            if not _merge_bindings(out, {symbol(k): boxed.datum}):
                return None

    return out


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    if is_pair(expr):
        for elem in unpack(expr):
            _collect_symbols(elem, out)


def _recurse_target(val):
    if val is None:
        return None
    if isinstance(val, BindingsResult):
        return val.values
    if isinstance(val, BindingResult):
        return [val.value] if val.value is not None else []
    if isinstance(val, FormalsResult):
        out = []
        for _kw, expr in (val.defaults or ()):
            out.append(expr)
        for _kw, expr in (val.kwonly or ()):
            out.append(expr)
        return out
    if isinstance(val, RepeatResult):
        out = []
        for res, _sp in val.matches:
            for v in res.values():
                if isinstance(v, MatchResult):
                    if v.source is not None:
                        out.append(v.source)
                    else:
                        out.append(v)
                elif v is not None and not isinstance(v, (dict, list)):
                    out.append(v)
        return out
    if isinstance(val, MatchResult):
        return val.source

    return val


def _tail_forms_for_binding_walk(rest):
    if rest is nil or not is_pair(rest):
        return []

    return [e for e in unpack(rest) if e is not None and e is not nil]


def _scope_region_keys(matched, specs):
    keys = set()
    for key, spec in (specs or {}).items():
        if spec is None:
            continue

        scope = getattr(spec, 'scope', None)
        scopes = getattr(spec, 'scopes', None)

        if scope is not None:
            keys.add(scope if isinstance(scope, str) else str(scope))

        if scopes is not None:
            for s in scopes:
                keys.add(s if isinstance(s, str) else str(s))
    if not keys:
        for key, spec in (specs or {}).items():
            if is_scope_region_spec(spec):
                keys.add(key)
    return keys


def _collect_binding_symbols_matcher(template, find_compiled):
    out = set()
    if template is nil or not is_pair(template):
        return out

    head, rest = template
    if not is_symbol(head):
        for elem in _tail_forms_for_binding_walk(rest):
            out |= _collect_binding_symbols_matcher(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    special = find_compiled(head)
    if special is None or not getattr(special, 'forms', None):
        for elem in _tail_forms_for_binding_walk(rest):
            out |= _collect_binding_symbols_matcher(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    try:
        m = match(rest, special.forms, with_specs=True)
    except SpecialError:
        m = None
    if m is None:
        for elem in _tail_forms_for_binding_walk(rest):
            out |= _collect_binding_symbols_matcher(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    matched, specs = m
    scope_keys = _scope_region_keys(matched, specs)

    for key, spec in specs.items():
        if is_binder_spec(spec) and key in matched:
            for sym in extract_binder_symbols(spec, matched[key]):
                out.add(sym)

    for key in scope_keys:
        if key in matched:
            val = matched[key]
            if val is not None:
                target = _recurse_target(val)
                if target is None:
                    pass
                elif is_pair(target):
                    for elem in unpack(target):
                        out |= _collect_binding_symbols_matcher(
                            elem if is_pair(elem) else cons(elem, nil),
                            find_compiled)
                elif isinstance(target, (list, tuple)):
                    for v in target:
                        out |= _collect_binding_symbols_matcher(
                            v if is_pair(v) else cons(v, nil),
                            find_compiled)
                else:
                    out |= _collect_binding_symbols_matcher(
                        target if is_pair(target) else cons(target, nil),
                        find_compiled)

    for key, spec in specs.items():
        if is_binder_spec(spec) and key in matched:
            val = matched[key]
            targets = getattr(spec, 'get_recurse_targets', lambda v: None)(val)
            if targets is not None:
                for v in targets:
                    if v is not None:
                        out |= _collect_binding_symbols_matcher(
                            v if is_pair(v) else cons(v, nil),
                            find_compiled)

    return out


def _repeat_count(template, bindings):
    syms = set()
    _collect_symbols(template, syms)
    counts = {
        len(bindings[s])
        for s in syms
        if s in bindings and isinstance(bindings[s], list)
    }
    if not counts:
        return 0
    if len(counts) != 1:
        raise CompilerError("incompatible ellipsis repetition lengths", template)
    return next(iter(counts))


def _expand_template(template, bindings, binding_symbols, compiler, introduced, index=None):
    if is_symbol(template):
        if template in bindings:
            val = bindings[template]
            if isinstance(val, list):
                if index is None:
                    raise CompilerError("ellipsis variable outside repeated template", template)
                return val[index]
            return val

        if _is_syntax_ellipsis(template):
            return template

        if template in binding_symbols:
            if template not in introduced:
                introduced[template] = gensym(str(template))
            return introduced[template]

        return template

    if not is_pair(template):
        return template

    items = _to_list(template)
    out = []
    i = 0
    while i < len(items):
        elem = items[i]
        rep = (i + 1 < len(items) and _is_syntax_ellipsis(items[i + 1]))
        if not rep:
            out.append(_expand_template(elem, bindings, binding_symbols, compiler, introduced, index=index))
            i += 1
            continue

        count = _repeat_count(elem, bindings)
        for rep_i in range(count):
            out.append(_expand_template(elem, bindings, binding_symbols, compiler, introduced, index=rep_i))
        i += 2

    return build_proper(*out)


def _extract_function_spec(fragment):
    fspec = None
    for op, arg in fragment.instructions:
        if op == OpCode.KOS_OP_CONST and isinstance(arg, FunctionSpec):
            fspec = arg

    if fspec is None:
        raise CompilerError("internal: missing FunctionSpec in macro lambda")

    return fspec


def _function_from_spec(compiler, fspec):
    from .lib import Function

    return Function(fspec.code, vars(compiler.module), fspec.formals_spec)


def _compile_macro_lambda(compiler, pattern_src, body_forms, literals, raw_rule):
    if not body_forms:
        raise CompilerError("macro-rules rule requires a body", raw_rule)

    pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
    vars_ordered = collect_pattern_variable_symbols(pattern_src, literals)
    formals = build_proper(*vars_ordered)

    if len(body_forms) == 1:
        body_expr = body_forms[0]
    else:
        body_expr = cons(symbol("compile/begin"), build_proper(*body_forms))

    lambda_src = cons(
        symbol("compile/lambda"),
        cons(formals, cons(body_expr, nil)))

    from .compile.helpers import SPECIALS

    lam_special = SPECIALS[symbol("compile/lambda")]
    frag = lam_special.compile_impl(compiler, lambda_src, False)
    fspec = _extract_function_spec(frag)

    return pattern_matcher, tuple(vars_ordered), lambda_src, fspec


def _maybe_unwrap_repeat_macro_kwarg(val, formal_sym):
    if not isinstance(val, list) or not val:
        return val

    for row in val:
        if not isinstance(row, dict) or len(row) != 1 or formal_sym not in row:
            return val

    return [row[formal_sym] for row in val]


@dataclass(frozen=True)
class SyntaxRule:
    pattern_matcher: Matcher
    template: object

    def to_tuple(self):
        return (self.pattern_matcher.to_tuple(), _serialize_form(self.template))

    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 2:
            raise ValueError("invalid syntax rule tuple")
        return cls(matcher_from_tuple(tup[0]), _deserialize_form(tup[1]))

    def try_expand(self, compiler, source_obj, literals):
        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        bindings = _syntax_match_result_to_bindings(raw)
        if bindings is None:
            return None

        binding_symbols = _collect_binding_symbols_matcher(
            self.template, compiler.find_compiled)
        introduced = {}
        return _expand_template(
            self.template,
            bindings,
            binding_symbols,
            compiler,
            introduced)


@dataclass(frozen=True)
class SyntaxRulesSegment:
    literals: frozenset
    rules: tuple


class SyntaxRulesTransformer:
    def __init__(self, name, literals, rules):
        self.name = name
        self.segments = (
            SyntaxRulesSegment(frozenset(literals), tuple(rules)),
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
                "SyntaxRulesTransformer.to_tuple requires exactly one "
                "segment until multi-segment const-pool format exists")

        seg = self.segments[0]
        return (
            _tag_syntax_rules,
            _serialize_form(self.name),
            tuple(_serialize_form(x) for x in seg.literals),
            tuple(r.to_tuple() for r in seg.rules),
        )

    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 4 or tup[0] is not _tag_syntax_rules:
            raise ValueError("invalid syntax-rules tuple")

        name = _deserialize_form(tup[1])
        literals = frozenset(_deserialize_form(x) for x in tup[2])
        rules = tuple(SyntaxRule.from_tuple(rt) for rt in tup[3])
        return cls.from_segments(name, (SyntaxRulesSegment(literals, rules),))

    def compile_impl(self, compiler, source_obj, tc=False):
        for segment in self.segments:
            for rule in segment.rules:
                expanded = rule.try_expand(
                    compiler, source_obj, segment.literals)
                if expanded is not None:
                    return expanded

        raise CompilerError("no matching define-syntax rule", source_obj)


@dataclass
class MacroRule:
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
        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        pos_fb = get_position(source_obj) if is_pair(source_obj) else None
        kwargs = _match_dict_to_syntax_kwargs(raw, position_fallback=pos_fb)
        if kwargs is None:
            return None

        for s in self.formals:
            if s not in kwargs:
                return None

        fspec = self.function_spec
        if fspec is None:
            from .compile.helpers import SPECIALS

            lam_special = SPECIALS[symbol("compile/lambda")]
            frag = lam_special.compile_impl(compiler, self.lambda_source, False)
            fspec = _extract_function_spec(frag)

        fn = _function_from_spec(compiler, fspec)
        args = [_maybe_unwrap_repeat_macro_kwarg(kwargs[s], s) for s in self.formals]
        return demand_call(fn, *args)


@dataclass(frozen=True)
class MacroRulesSegment:
    literals: frozenset
    rules: tuple


class MacroRulesTransformer:
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


def build_syntax(name, tagged_tuple):
    transformer = SyntaxRulesTransformer.from_tuple(tagged_tuple)
    if name is not None:
        transformer.name = name
    return transformer


def build_macro_syntax(name, tagged_tuple):
    transformer = MacroRulesTransformer.from_tuple(tagged_tuple)
    if name is not None:
        transformer.name = name
    return transformer


def build_syntax_rules_transformer(name, spec):
    head_m = match(spec, _SYNTAX_RULES_HEAD_PATTERN)
    if head_m is None:
        raise CompilerError("define-syntax requires a syntax-rules form", spec)

    literals_expr = head_m["literals"].source
    rules_expr = head_m["rules"].source

    literals = frozenset(_to_list(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _SYNTAX_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise CompilerError("syntax-rules requires at least one rule", spec)

    rules = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise CompilerError("invalid syntax-rules rule", raw_rule)

        pattern_src, template_tail = raw_rule
        if not is_proper(template_tail) or cdr(template_tail) is not nil:
            raise CompilerError(
                "syntax-rules rule must have pattern and template",
                raw_rule)
        template_expr = car(template_tail)
        pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
        rules.append(SyntaxRule(pattern_matcher, template_expr))

    return SyntaxRulesTransformer(name, literals, rules)


def build_macro_rules_transformer(compiler, name, spec):
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


# The end.
