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
koskari.compile.syntax

Syntax rules transformer implementing (syntax-rules ...)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from dataclasses import dataclass

from ..exceptions import CompilerError
from ..lib import car, cdr, cons, nil, symbol
from ..matcher import (BindingsResult, BindingResult, extract_binder_symbols,
                       FormalsResult,
                       is_binder_spec, is_scope_region_spec, match)
from ..types import is_pair, is_proper, is_symbol, unpack


_ellipsis = symbol("...")
_tag_syntax_rules = symbol("syntax/rules")
_tag_list = symbol("syntax/list")
_tag_cons = symbol("syntax/cons")


def _to_list(value):
    if value is nil:
        return []
    if not is_proper(value):
        raise CompilerError("expected proper list", value)
    return list(unpack(value))


def _from_list(values):
    if not values:
        return nil
    return cons(*values, nil)


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
            return _from_list([_deserialize_form(x) for x in payload[1:]])

        if tag is _tag_cons:
            if len(payload) != 3:
                raise ValueError("invalid cons payload")
            return cons(_deserialize_form(payload[1]), _deserialize_form(payload[2]))

        return tuple(_deserialize_form(x) for x in payload)

    if isinstance(payload, frozenset):
        return frozenset(_deserialize_form(x) for x in payload)

    return payload


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


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    if is_pair(expr):
        for elem in unpack(expr):
            _collect_symbols(elem, out)


def _recurse_target(val):
    """
    Return the value(s) to recurse into for binding symbol collection.

    For BindingsResult: values list. For FormalsResult: defaults and kwonly.
    For MatchResult or has .source: source. Else: val itself.
    """
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
    if hasattr(val, 'source'):
        return val.source
    return val


def _scope_region_keys(matched, specs):
    """
    Return set of keys that are scope regions from specs and binder metadata
    """

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
    """
    Walk template and return set of symbols in binding position. Uses
    find_compiled and special.forms matchers for metadata-driven discovery.
    """

    out = set()
    if template is nil or not is_pair(template):
        return out

    head, rest = template
    if not is_symbol(head):
        for elem in (unpack(rest) if rest is not nil and is_pair(rest) else []):
            if elem is not None and elem is not nil:
                out |= _collect_binding_symbols_matcher(
                    elem if is_pair(elem) else cons(elem, nil),
                    find_compiled)
        return out

    special = find_compiled(head)
    if special is None or not getattr(special, 'forms', None):
        for elem in (unpack(rest) if rest is not nil and is_pair(rest) else []):
            if elem is not None and elem is not nil:
                out |= _collect_binding_symbols_matcher(
                    elem if is_pair(elem) else cons(elem, nil),
                    find_compiled)
        return out

    m = match(rest, special.forms, with_specs=True)
    if m is None:
        for elem in (unpack(rest) if rest is not nil and is_pair(rest) else []):
            if elem is not None and elem is not nil:
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


def _match_pattern(pattern, expr, literals):
    if is_symbol(pattern):
        if pattern is _ellipsis:
            return None
        if pattern in literals:
            return {} if pattern == expr else None
        return {pattern: expr}

    if not is_pair(pattern):
        return {} if pattern == expr else None

    if not (expr is nil or is_pair(expr)):
        return None

    p_items = _to_list(pattern)
    e_items = _to_list(expr)
    return _match_sequence(p_items, e_items, literals)


def _match_sequence(p_items, e_items, literals):
    def rec(pi, ei, bindings):
        if pi >= len(p_items):
            return bindings if ei == len(e_items) else None

        pat = p_items[pi]
        rep = (pi + 1 < len(p_items) and p_items[pi + 1] is _ellipsis)

        if not rep:
            if ei >= len(e_items):
                return None
            m = _match_pattern(pat, e_items[ei], literals)
            if m is None:
                return None
            nxt = dict(bindings)
            if not _merge_bindings(nxt, m):
                return None
            return rec(pi + 1, ei + 1, nxt)

        next_pi = pi + 2
        for used in range(0, len(e_items) - ei + 1):
            nxt = dict(bindings)
            ok = True
            repeated = {}

            for off in range(used):
                m = _match_pattern(pat, e_items[ei + off], literals)
                if m is None:
                    ok = False
                    break
                for k, v in m.items():
                    repeated.setdefault(k, []).append(v)

            if not ok:
                continue

            for k, vals in repeated.items():
                if k in nxt:
                    if nxt[k] != vals:
                        ok = False
                        break
                else:
                    nxt[k] = vals

            if not ok:
                continue

            out = rec(next_pi, ei + used, nxt)
            if out is not None:
                return out

        return None

    return rec(0, 0, {})


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

        if template is _ellipsis:
            return template

        if template in binding_symbols:
            if template not in introduced:
                introduced[template] = symbol(compiler.gen_label("syntax_%s" % str(template)))
            return introduced[template]

        return template

    if not is_pair(template):
        return template

    items = _to_list(template)
    out = []
    i = 0
    while i < len(items):
        elem = items[i]
        rep = (i + 1 < len(items) and items[i + 1] is _ellipsis)
        if not rep:
            out.append(_expand_template(elem, bindings, binding_symbols, compiler, introduced, index=index))
            i += 1
            continue

        count = _repeat_count(elem, bindings)
        for rep_i in range(count):
            out.append(_expand_template(elem, bindings, binding_symbols, compiler, introduced, index=rep_i))
        i += 2

    return _from_list(out)


@dataclass(frozen=True)
class SyntaxRule:
    pattern: object
    template: object


    def to_tuple(self):
        return (_serialize_form(self.pattern), _serialize_form(self.template))


    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 2:
            raise ValueError("invalid syntax rule tuple")
        return cls(_deserialize_form(tup[0]), _deserialize_form(tup[1]))


class SyntaxRulesTransformer:
    """
    Compiled-compatible transformer built from (syntax-rules ...).
    """

    def __init__(self, name, literals, rules):
        self.name = name
        self.literals = frozenset(literals)
        self.rules = tuple(rules)


    def to_tuple(self):
        return (
            _tag_syntax_rules,
            _serialize_form(self.name),
            tuple(_serialize_form(x) for x in self.literals),
            tuple(r.to_tuple() for r in self.rules),
        )


    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 4 or tup[0] is not _tag_syntax_rules:
            raise ValueError("invalid syntax-rules tuple")

        name = _deserialize_form(tup[1])
        literals = [_deserialize_form(x) for x in tup[2]]
        rules = [SyntaxRule.from_tuple(rt) for rt in tup[3]]
        return cls(name, literals, rules)


    def compile_impl(self, compiler, source_obj, tc=False):
        for rule in self.rules:
            bindings = _match_pattern(rule.pattern, source_obj, self.literals)
            if bindings is None:
                continue

            binding_symbols = _collect_binding_symbols_matcher(
                rule.template, compiler.find_compiled)
            introduced = {}
            return _expand_template(
                rule.template,
                bindings,
                binding_symbols,
                compiler,
                introduced)

        raise CompilerError("no matching define-syntax rule", source_obj)


def build_syntax(name, tagged_tuple):
    """
    Runtime helper: deserialize a tagged syntax-rules tuple.
    """

    transformer = SyntaxRulesTransformer.from_tuple(tagged_tuple)
    if name is not None:
        transformer.name = name
    return transformer


def build_syntax_rules_transformer(name, spec):
    """
    Parse (syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...) into transformer.
    """

    if not is_pair(spec):
        raise CompilerError("define-syntax requires a syntax-rules form", spec)

    head, tail = spec
    if not (is_symbol(head) and str(head) == "syntax-rules"):
        raise CompilerError("define-syntax supports syntax-rules only", spec)

    if not is_pair(tail):
        raise CompilerError("syntax-rules requires literals list", spec)

    literals_expr, rules_expr = tail
    literals = frozenset(_to_list(literals_expr))
    # Syntax name is implicitly a literal for pattern matching
    literals = literals | {name}
    rules = []

    if rules_expr is nil:
        raise CompilerError("syntax-rules requires at least one rule", spec)

    for raw_rule in unpack(rules_expr):
        if not is_pair(raw_rule):
            raise CompilerError("invalid syntax-rules rule", raw_rule)
        pattern_expr, template_tail = raw_rule
        if not is_pair(template_tail) or template_tail[1] is not nil:
            raise CompilerError("syntax-rules rule must have pattern and template", raw_rule)
        template_expr = template_tail[0]
        rules.append(SyntaxRule(pattern_expr, template_expr))

    return SyntaxRulesTransformer(name, literals, rules)


# The end.
