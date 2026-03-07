"""
koskari.compile.syntax

Data-driven syntax-rule transformer objects used by define-syntax.
"""


from dataclasses import dataclass

from ..exceptions import CompilerError
from ..lib import car, cdr, cons, nil, symbol
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


def _form_head(expr):
    """
    Return the base name of a form (let, lambda, etc.) for binding detection.
    Handles both bare and compile/-prefixed symbols.
    """
    if not is_symbol(expr):
        return None
    s = str(expr)
    if s.startswith("compile/"):
        return s[8:]
    return s


def _collect_bindings_from_list(bindings_list, out, recurse):
    """
    Extract binding names from a let-style bindings list ((VAR VAL) ...).
    Only handles (SYM EXPR) pairs; (EXPR as: SYM) is deferred.
    recurse is called on each VAL for nested binding forms.
    """
    if not is_pair(bindings_list):
        return
    for elem in unpack(bindings_list):
        if is_pair(elem):
            var, val_tail = elem
            if is_pair(val_tail):
                val = car(val_tail)
                if is_symbol(var):
                    out.add(var)
                out |= recurse(val)


def _collect_formals_symbols(args, out):
    """
    Extract parameter symbols from lambda formals (list or rest symbol).
    """
    if is_symbol(args):
        out.add(args)
        return
    if is_pair(args):
        for elem in unpack(args):
            if is_symbol(elem):
                out.add(elem)


def _collect_binding_symbols(template):
    """
    Walk template and return set of symbols in binding position.
    Recognizes let, lambda, let/cc, capture/cc (bare and compile/ prefixed).
    """

    out = set()
    if not is_pair(template):
        return out
    items = list(unpack(template))
    if not items:
        return out
    head = items[0]
    base = _form_head(head)
    rest = items[1:]
    if not rest:
        return out

    if base == "let":
        if len(rest) >= 3 and is_symbol(rest[0]):
            name, bindings, body = rest[0], rest[1], rest[2]
            out.add(name)
            _collect_bindings_from_list(bindings, out, _collect_binding_symbols)
            out |= _collect_binding_symbols(body)
            for r in rest[3:]:
                out |= _collect_binding_symbols(r)
        else:
            bindings, body = rest[0], rest[1]
            _collect_bindings_from_list(bindings, out, _collect_binding_symbols)
            out |= _collect_binding_symbols(body)
            for r in rest[2:]:
                out |= _collect_binding_symbols(r)

    elif base == "lambda":
        if len(rest) >= 3 and is_symbol(rest[0]):
            name, formals, body = rest[0], rest[1], rest[2]
            out.add(name)
            _collect_formals_symbols(formals, out)
            out |= _collect_binding_symbols(body)
            for r in rest[3:]:
                out |= _collect_binding_symbols(r)
        else:
            formals, body = rest[0], rest[1]
            _collect_formals_symbols(formals, out)
            out |= _collect_binding_symbols(body)
            for r in rest[2:]:
                out |= _collect_binding_symbols(r)

    elif base in ("let/cc", "capture/cc") and len(rest) >= 1 and is_symbol(rest[0]):
        out.add(rest[0])
        for r in rest[1:]:
            out |= _collect_binding_symbols(r)

    else:
        for elem in rest:
            out |= _collect_binding_symbols(elem)

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

            binding_symbols = _collect_binding_symbols(rule.template)
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
