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
koskari.compile.helpers_ng

Compile helpers for matcher_ng FORMALS captures (GROUP dict payload).
Does not import koskari.compile.helpers

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from typing import List

from ..exceptions import ParametersError, SpecialError
from ..lib import (Datum, Matcher, OpCode, car, cdr, cons, keyword, nil, pair,
                   symbol)
from ..matcher_ng import (BINDING, BINDINGS, FORMALS, FORMALS_SOURCE_KEY, LIST,
                          LITERAL_KW, MATCH, REST, SYMBOL, USING, USING_UNPACK,
                          VALUE, HygienicMatchCompiler, Pattern)
from ..types import (is_keyword, is_pair, is_proper,
                     is_symbol, unpack)
from .compiler import ByteCodeFragment


__all__ = (
    "BINDING",
    "FORMALS",
    "BINDINGS",
    "USING",
    "USING_UNPACK",
    "const_only_expr",
    "convert_bindings",
    "discover_docstring",
    "gather_bindings",
    "gather_formals",
    "gather_parameters_ng",
    "helper_begin",
    "helper_binding",
    "helper_elsebody",
    "helper_using",
    "is_lazygensym",
    "is_symbolish",
    "match",
    "match_pattern_raw",
    "match1",
    "parse_decl",
    "pycons",
    "requote",
    "strip_leading_docstring",
    "unpack_lhs_bind_symbols",
    "using_unpack_helper",
)


def is_lazygensym(x):
    """
    Return True if x is a lazy gensym (compile-time placeholder).
    Koskari currently has no lazy gensyms; always False
    """

    return False


_SYM_BEGIN = symbol('begin')
_SYM_COMPILE_BEGIN = symbol('compile/begin')
_BEGIN_HEADS = (_SYM_BEGIN, _SYM_COMPILE_BEGIN)


def match_pattern_raw(source, patterns, literals=None):
    """
    Try each matcher_ng pattern against ``source`` (pair spine or ``nil``).
    Return native ``MatchResult.captures`` on first match, else ``None``.

    Used by :meth:`koskari.compile.compiler.Special.match_for_hygiene` and
    :func:`koskari.hygiene_ng.collect_binding_symbols`.
    """

    if source is not nil and not is_pair(source):
        raise TypeError(
            f"match source must be nil or a pair, got {source!r}")

    if not isinstance(patterns, (list, tuple)):
        patterns = [patterns]

    for pat in patterns:
        if not isinstance(pat, Pattern):
            raise TypeError(
                "compile helpers_ng.match_pattern_raw expects matcher_ng Pattern,"
                f" got {pat!r}")
        hc = HygienicMatchCompiler()
        bm = Matcher.from_tuple(hc.lower(pat))
        m = bm.match(source)
        if m.matched:
            return m.captures

    return None


def match(
        source,
        patterns,
        literals=None,
        with_specs=False):
    """
    Try each matcher_ng pattern against ``source`` (pair spine or ``nil``).
    Returns native ``MatchResult.captures`` on first match, else ``None``.
    """

    raw = match_pattern_raw(source, patterns, literals=literals)
    if raw is None:
        return None
    if with_specs:
        return raw, {}
    return raw


def match1(
        value,
        patterns,
        literals=None,
        with_specs=False):
    """
    Match a single element by wrapping ``cons(value, nil)``
    """

    return match(cons(value, nil), patterns, literals=literals,
                 with_specs=with_specs)


_keyword_star = keyword("*")
_keyword_starstar = keyword("**")

_PYCONS_DIRECT = (
    str, bytes, bool, int, float, complex,
    type(None), type(...),
    symbol, keyword, type(nil), pair,
)
_PYCONS_RECONS = (tuple, list)


_BINDING_FORMS = [
    LIST(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    LIST(SYMBOL("name"), VALUE("expr")),
]


def is_symbolish(thing):
    """
    Return True if thing is a symbol or lazy gensym (valid binding name)
    """

    return is_symbol(thing) or is_lazygensym(thing)


def helper_binding(source):
    """
    Parse a single binding form ``(SYM EXPR)`` or ``(EXPR as: SYM)``
    """

    msg = ("a binding must be in the form of (SYM EXPR) or (EXPR as: SYM),"
           " not %s" % source)

    if not is_proper(source):
        raise SpecialError(msg, source)

    m = match1(source, _BINDING_FORMS)
    if m is None:
        raise SpecialError(msg, source)

    name = m.get("name")
    value = m.get("expr")

    return (
        name.datum if name else None,
        value.datum)


_USING_FORMS = (
    *_BINDING_FORMS,
    LIST(VALUE("expr")),
)


def helper_using(source):
    """
    Parse a single using form ``(EXPR)``, ``(SYM EXPR)``, or ``(EXPR as: SYM)``
    """

    msg = ("a using must be in the form of (EXPR), (SYM EXPR),"
           " or (EXPR as: SYM), not %s" % source)

    if not is_proper(source):
        raise SpecialError(msg, source)

    m = match1(source, _USING_FORMS)
    if m is None:
        raise SpecialError(msg, source)

    name_m = m.get("name")
    value = m.get("expr")

    name = name_m.datum if name_m else None

    return (name, value.datum)


def gather_bindings(bindings_source, source=None):
    """
    Parse a list of binding forms into parallel names and value expressions
    """

    names = []
    values = []
    if not is_pair(bindings_source):
        return (names, values)
    for elem in bindings_source.unpack():
        name, val = helper_binding(elem)
        if name in names:
            raise SpecialError(
                f"duplicate declarations for {name} in bindings",
                source if source is not None else bindings_source)
        names.append(name)
        values.append(val)
    return (names, values)


def convert_bindings(bindings_source, source=None):
    """
    Turn a list of binding forms into a keyword/value proper list
    """

    names, values = gather_bindings(bindings_source, source)
    col = []
    for n, v in zip(names, values):
        col.append(keyword(n))
        col.append(v)
    return cons(*col, nil) if col else nil


def _formal_kw_tag_to_key(kw):
    """
    Map keyword atom to annotation key; empty keyword (``::`` sugar) is type
    """

    raw = str(kw)
    if raw.endswith(":"):
        raw = raw[:-1]
    if raw == "":
        return "type"
    return raw


def _parse_formal_tail(tail, source):
    """
    Parse REST after formal name: keyword/value pairs or lone default
    """

    if tail is nil:
        return {}, None
    head = car(tail)
    rest = cdr(tail)
    if not is_keyword(head):
        if rest is not nil:
            raise SpecialError(
                "malformed formal tail after name, not %r" % tail,
                source)
        return {}, head

    ann = {}
    default = None
    cur = tail
    while cur is not nil:
        kw = car(cur)
        cur = cdr(cur)
        if not is_keyword(kw):
            raise SpecialError(
                "expected keyword in formal attribute list, not %r"
                % kw,
                source)
        if cur is nil:
            raise SpecialError(
                "missing value for formal attribute %r" % kw,
                source)
        val = car(cur)
        cur = cdr(cur)
        key = _formal_kw_tag_to_key(kw)
        if key == "value":
            default = val
        elif key == "type":
            ann["type"] = val
        else:
            raise SpecialError(
                "unknown formal attribute %r" % head,
                source)
    return ann, default


def parse_formal_parameter(form, source=None):
    """
    Parse one formal parameter: bare symbol, or list with name and attributes
    """

    if is_symbol(form) or is_lazygensym(form):
        return (form, {}, None)
    if not is_pair(form):
        raise SpecialError(
            "formal must be symbol or proper list, not %r" % form,
            source)
    if not is_proper(form):
        raise SpecialError(
            "formal list must be proper, not %r" % form,
            source)

    elems = tuple(unpack(form))
    if len(elems) == 1 and is_symbolish(elems[0]):
        return (elems[0], {}, None)

    _sym_expr = (LIST(SYMBOL("name"), VALUE("expr")),)
    m_sv = match1(form, _sym_expr)
    if m_sv is not None:
        return (m_sv["name"].datum, {}, m_sv["expr"].datum)

    _expr_as_name = (
        LIST(VALUE("type_expr"), LITERAL_KW("as"), SYMBOL("name")),
    )
    m_as = match1(form, _expr_as_name)
    if m_as is not None:
        return (
            m_as["name"].datum,
            {"type": m_as["type_expr"].datum},
            None)

    _FORMAL_HEAD = (LIST(MATCH(SYMBOL("name"), REST("tail"))),)
    m = match1(form, _FORMAL_HEAD)
    if m is None:
        raise SpecialError("invalid formal parameter form %r" % form, source)

    name = m["name"].datum
    tail = m["tail"].datum
    ann, default = _parse_formal_tail(tail, source)
    return (name, ann, default)


def _parse_decl_tail(tail, source):
    """
    Parse REST after a declaration name: only keyword attributes, no value
    """

    if tail is nil:
        return {}
    head = car(tail)
    if not is_keyword(head):
        raise SpecialError(
            "declaration cannot include a value",
            source)

    ann = {}
    cur = tail
    while cur is not nil:
        kw = car(cur)
        cur = cdr(cur)
        if not is_keyword(kw):
            raise SpecialError(
                "expected keyword in declaration attribute list, not %r"
                % kw,
                source)
        if cur is nil:
            raise SpecialError(
                "missing value for declaration attribute %r" % kw,
                source)
        val = car(cur)
        cur = cdr(cur)
        key = _formal_kw_tag_to_key(kw)
        if key == "value":
            raise SpecialError(
                "value: is not allowed in declaration",
                source)
        elif key == "type":
            ann["type"] = val
        else:
            raise SpecialError(
                "unknown declaration attribute %r" % kw,
                source)
    return ann


def parse_decl(form, source=None):
    """
    Parse a binding-shaped declaration with no value
    """

    if is_symbol(form) or is_lazygensym(form):
        return (form, None)
    if not is_pair(form):
        raise SpecialError(
            "declaration must be symbol or proper list, not %r" % form,
            source)
    if not is_proper(form):
        raise SpecialError(
            "declaration list must be proper, not %r" % form,
            source)

    elems = tuple(unpack(form))
    if len(elems) == 1 and is_symbolish(elems[0]):
        return (elems[0], None)

    _sym_expr = (LIST(SYMBOL("name"), VALUE("expr")),)
    m_sv = match1(form, _sym_expr)
    if m_sv is not None:
        raise SpecialError(
            "declaration cannot include a value",
            source)

    _expr_as_name = (
        LIST(VALUE("type_expr"), LITERAL_KW("as"), SYMBOL("name")),
    )
    m_as = match1(form, _expr_as_name)
    if m_as is not None:
        return (m_as["name"].datum, m_as["type_expr"].datum)

    _FORMAL_HEAD = (LIST(MATCH(SYMBOL("name"), REST("tail"))),)
    m = match1(form, _FORMAL_HEAD)
    if m is None:
        raise SpecialError("invalid declaration form %r" % form, source)

    name = m["name"].datum
    tail = m["tail"].datum
    ann = _parse_decl_tail(tail, source)
    type_expr = ann.get("type")
    return (name, type_expr)


def gather_parameters_ng(args, source=None):
    """
    Parse call-site argument list into interleaved args_seq and kwds_seq
    """

    undefined = object()

    if is_symbol(args) or is_lazygensym(args):
        return ([("*", args)], [])

    elif isinstance(args, (list, tuple)):
        improper = False
        args = cons(*args, nil) if args else nil

    elif is_proper(args):
        improper = False

    elif is_pair(args):
        improper = True

    else:
        raise ParametersError(
            "parameters must be symbol or pair, not %r"
            % args, source)

    args_seq = []
    kwds_seq = []
    pos_index = 0
    in_kwds_phase = False

    iargs = iter(unpack(args))
    arg = next(iargs, undefined)

    while arg is not undefined:
        if is_keyword(arg):
            if arg is _keyword_star:
                if in_kwds_phase:
                    raise ParametersError(
                        "* args must come before keyword arguments",
                        source)
                star_sym = next(iargs, undefined)
                if star_sym is undefined:
                    raise ParametersError(
                        "* keyword parameter needs value",
                        source)
                args_seq.append(("*", star_sym))

            elif arg is _keyword_starstar:
                in_kwds_phase = True
                starstar_sym = next(iargs, undefined)
                if starstar_sym is undefined:
                    raise ParametersError(
                        "** keyword parameter needs value",
                        source)
                kwds_seq.append(("**", starstar_sym))

            else:
                in_kwds_phase = True
                value = next(iargs, undefined)
                if value is undefined:
                    raise ParametersError(
                        "missing value for keyword parameter %s" % arg, source)
                kwds_seq.append((str(arg), value))

        else:
            if in_kwds_phase:
                raise ParametersError(
                    "positional arguments must come before keywords", source)
            args_seq.append((pos_index, arg))
            pos_index += 1

        arg = next(iargs, undefined)

    if improper and args_seq:
        last = args_seq.pop()
        args_seq.append(("*", last[1]))

    return (args_seq, kwds_seq)


def pycons(val):
    """
    Turn a Python tuple or list into a proper list (nested pairs ending in nil)
    """

    if isinstance(val, _PYCONS_DIRECT):
        return val
    if isinstance(val, _PYCONS_RECONS):
        if not val:
            return nil
        return cons(*map(pycons, val), nil)
    raise TypeError(f"cannot pycons {val!r}")


def const_only_expr(fragment):
    """
    Check whether a ByteCodeFragment consists only of a single CONST value
    """

    found = False
    value = None

    for op, opa in fragment.instructions:
        if op == OpCode.KOS_OP_CONST:
            if found:
                return (False, None)
            found = True
            value = opa

        elif op in (OpCode.KOS_OP_NOOP, OpCode.KOS_OP_POSITION):
            continue

        elif found and op == OpCode.KOS_OP_POP and opa == 1:
            found = False
            value = None

        elif found:
            return (False, None)

    return (found, value)


def requote(
        source,
        cons_sym=None,
        quote_sym=None):
    """
    Rewrite a list form so each element is wrapped in ``(quote_sym ELEMENT)``
    """

    if cons_sym is None:
        cons_sym = symbol("compile/cons")
    if quote_sym is None:
        quote_sym = symbol("compile/quote")

    col = []
    while True:
        element, rest = source
        col.append(cons(quote_sym, element, nil))

        if rest is nil:
            col.append(nil)
            break
        elif not is_pair(rest):
            col.append(cons(quote_sym, rest, nil))
            break

        source = rest

    return cons(cons_sym, *col, nil)


def _strip_leading_docstring_body(body):
    """
    Return ``(docstring, new_body)`` after removing one leading docstring
    """

    while body is not nil:
        head, tail = body
        if isinstance(head, str):
            return head, tail

        if head is nil or not is_pair(head):
            return None, None

        ihead, itail = head
        if ihead in _BEGIN_HEADS:
            if itail is nil:
                body = tail
                continue
            doc, new_itail = _strip_leading_docstring_body(itail)
            if doc is None:
                return None, None
            if new_itail is nil:
                return doc, tail
            new_head = cons(ihead, new_itail)
            return doc, cons(new_head, tail)
        else:
            return None, None

    return None, None


def strip_leading_docstring(body):
    """
    If the leading form(s) are a docstring, return ``(doc, body')``
    """

    doc, new_body = _strip_leading_docstring_body(body)
    if doc is None:
        return None, body
    return doc, new_body


_keyword_else = keyword("else")


def helper_elsebody(source):
    """
    Strip a trailing ``[else: EXPR]`` clause from a body list
    """

    collected = []
    current = source

    while current is not nil:
        head, tail = current
        if tail is nil and is_pair(head):
            e, ebody = head
            if e is _keyword_else:
                body_list = cons(*collected, nil) if collected else nil
                if ebody is nil:
                    return body_list, None
                else:
                    return body_list, ebody[0]
        collected.append(head)
        current = tail

    body_list = cons(*collected, nil) if collected else nil
    return body_list, nil


def unpack_lhs_bind_symbols(lhs, source=None, flatten=False):
    """
    Yield each symbol introduced by an unpack LHS
    """

    ctx = source if source is not None else lhs

    def fail(msg):
        raise SpecialError(msg, ctx)

    if is_symbolish(lhs):
        yield lhs
        return

    if not is_proper(lhs):
        fail("unpack pattern must be a symbol or a proper list")

    items = list(unpack(lhs))
    if not items:
        fail("empty unpack pattern")

    star_idx = [
        i for i, elt in enumerate(items)
        if is_keyword(elt) and elt is _keyword_star]

    if len(star_idx) > 1:
        fail("at most one * is allowed in an unpack pattern")

    if not star_idx:
        for elt in items:
            if flatten and is_pair(elt):
                yield from unpack_lhs_bind_symbols(elt, source, flatten)
            elif not is_symbolish(elt):
                fail("unpack pattern list elements must be symbols")
            else:
                yield elt
        return

    si = star_idx[0]
    prefix = items[:si]
    after_star = items[si + 1:]
    if not after_star:
        fail("* in unpack pattern must be followed by a binding name")

    star_name = after_star[0]
    suffix = after_star[1:]
    if not is_symbolish(star_name):
        fail("name after * in unpack pattern must be a symbol")

    for elt in prefix:
        if flatten and is_pair(elt):
            yield from unpack_lhs_bind_symbols(elt, source, flatten)
        elif not is_symbolish(elt):
            fail("unpack pattern list elements must be symbols")
        else:
            yield elt

    yield star_name

    for elt in suffix:
        if flatten and is_pair(elt):
            yield from unpack_lhs_bind_symbols(elt, source, flatten)
        elif not is_symbolish(elt):
            fail("unpack pattern list elements must be symbols")
        else:
            yield elt


def using_unpack_helper(compiler, lhs, source=None, declare=True):
    """
    Build bytecode to bind names from TOS without compiling an RHS
    """

    ctx = source if source is not None else lhs

    def fail(msg):
        raise SpecialError(msg, ctx)

    def maybe_declare(var_name):
        if declare:
            frag.declare_var(var_name)

    frag = compiler.fragment("using.unpack")
    if source is not None:
        frag.position_of(source)

    if is_symbolish(lhs):
        name = str(lhs)
        maybe_declare(name)
        frag.instruction(OpCode.KOS_OP_VAR_STORE, name)
        return frag

    if not is_proper(lhs):
        fail("unpack pattern must be a symbol or a proper list")

    items = list(unpack(lhs))
    if not items:
        fail("empty unpack pattern")

    if len(items) == 2 \
       and is_symbolish(items[0]) \
       and is_symbolish(items[1]):
        name1 = str(items[0])
        name2 = str(items[1])
        maybe_declare(name1)
        maybe_declare(name2)
        frag.instruction(OpCode.KOS_OP_UNPACK_VAR, (name1, name2, 0, 0))
        return frag

    star_idx = [
        i for i, elt in enumerate(items)
        if is_keyword(elt) and elt is _keyword_star]

    if len(star_idx) > 1:
        fail("at most one * is allowed in an unpack pattern")

    if not star_idx:
        n = len(items)
        frag.instruction(OpCode.KOS_OP_UNPACK, n)
        for elt in items:
            if is_pair(elt):
                frag.merge(using_unpack_helper(compiler, elt, source, declare))
            elif is_symbolish(elt):
                elt = str(elt)
                maybe_declare(elt)
                frag.instruction(OpCode.KOS_OP_VAR_STORE, elt)
            else:
                fail("unpack pattern list elements must be symbols or pairs")
        return frag

    si = star_idx[0]
    prefix = items[:si]
    after_star = items[si + 1:]
    if not after_star:
        fail("* in unpack pattern must be followed by a binding name")

    star_name = after_star[0]
    suffix = after_star[1:]
    if not is_symbolish(star_name):
        fail("name after * in unpack pattern must be a symbol")
    star_name = str(star_name)

    if not prefix and not suffix:
        maybe_declare(star_name)
        frag.instruction(OpCode.KOS_OP_VAR_STORE, star_name)
        return frag

    left = len(prefix)
    right = len(suffix)
    frag.instruction(OpCode.KOS_OP_UNPACK_STAR, (left, right, 0, 0))
    for elt in prefix:
        if is_pair(elt):
            frag.merge(using_unpack_helper(compiler, elt, source, declare))
        elif is_symbolish(elt):
            elt = str(elt)
            maybe_declare(elt)
            frag.instruction(OpCode.KOS_OP_VAR_STORE, elt)
        else:
            fail("unpack pattern list elements must be symbols or pairs")

    maybe_declare(star_name)
    frag.instruction(OpCode.KOS_OP_VAR_STORE, star_name)

    for elt in suffix:
        if is_pair(elt):
            frag.merge(using_unpack_helper(compiler, elt, source, declare))
        elif is_symbolish(elt):
            elt = str(elt)
            maybe_declare(elt)
            frag.instruction(OpCode.KOS_OP_VAR_STORE, elt)
        else:
            fail("unpack pattern list elements must be symbols or pairs")

    return frag


def discover_docstring(body):
    """
    Return the leading docstring str from a body list, or None.

    Does not mutate body. Leading ``begin`` forms are peeled for discovery
    only; the body is still compiled in full elsewhere

    :param body: proper list of forms (pair or nil)
    :return: str or None
    """

    while body is not nil:
        head, tail = body
        if isinstance(head, str):
            return head

        if head is nil or not is_pair(head):
            return None

        ihead, itail = head
        if ihead in _BEGIN_HEADS:
            if itail is nil:
                body = tail
            else:
                body = itail
        else:
            return None

    return None


def helper_begin(
        fragment: ByteCodeFragment,
        body: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile a body list into a fragment

    :param fragment: the fragment to compile into
    :param body: the body list to compile
    :param tc: whether to compile in tail call optimization mode
    :return: the compiled fragment
    """

    if body is nil:
        fragment.instruction(OpCode.KOS_OP_CONST, None)
        return fragment

    compiler = fragment.compiler

    while True:
        fragment.position_of(body)

        expr, body = body
        if body is nil:
            fragment.merge(compiler.full_compile(expr, tc))
            break
        else:
            fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.KOS_OP_POP, 1)

    return fragment


def _merge_formal_annotations(annotations, name_sym, ann_bundle):
    """
    Merge ann_bundle into annotations dict keyed by str(name_sym)
    """

    if not ann_bundle:
        return
    key = str(name_sym)
    if key in annotations:
        annotations[key].update(ann_bundle)
    else:
        annotations[key] = dict(ann_bundle)


def _attrs_frames_to_ann(attr_frames: List[dict], source):
    """
    Fold formal attribute REPEAT frames into (ann dict, default or None)
    """

    ann = {}
    default = None
    for afr in attr_frames:
        kw_cell = afr.get('kw')
        if kw_cell is None:
            continue
        kw = kw_cell.datum
        val_cell = afr.get('attr_val')
        if val_cell is None:
            raise SpecialError(
                f"missing value for formal attribute {kw!r}",
                source)
        val = val_cell.datum

        if kw == keyword('value'):
            default = val
        elif kw in (keyword('type'), keyword('')):
            ann['type'] = val
        else:
            raise SpecialError(
                f"unknown formal attribute {kw!r}",
                source)
    return ann, default


def _gather_one_positional_frame(fr: dict, source):
    """
    Map one positional REPEAT frame to (name, ann_bundle, default_expr)
    """

    name = None if (v := fr.get('name')) is None else v.datum
    typ = None if (v := fr.get('type')) is None else v.datum

    default_cell = fr.get('default')
    if default_cell is None:
        default_expr = None
    else:
        default_expr = default_cell.datum

    annotations = {}
    if typ is not None:
        annotations['type'] = typ

    attrs_cell = fr.get('attrs')
    if attrs_cell:
        ann2, default2 = _attrs_frames_to_ann(attrs_cell, source)
        for k2, v2 in ann2.items():
            if k2 in annotations and annotations[k2] != v2:
                raise SpecialError(
                    f"conflicting formal annotations for {name!r}",
                    source)
            annotations[k2] = v2
        if default2 is not None:
            if default_expr is not None:
                raise SpecialError(
                    f"duplicate default for formal {name!r}",
                    source)
            default_expr = default2

    if name is None:
        raise SpecialError(
            f"positional frame missing name: {fr!r}",
            source)
    if not (is_symbol(name) or is_lazygensym(name)):
        raise SpecialError(
            "positional formals must be symbols or bracket forms, not"
            f" {name!r}",
            source)

    return name, annotations, default_expr


def gather_formals(formals_group, source=None):
    """
    Parse matcher_ng FORMALS GROUP dict into six values for compile specials

    Tuple order matches historical compile formals unpacking:
    (positional, defaults, kwonly, star, starstar, annotations)

    :param formals_group: dict from captures[formals_slot] after match
    :param source: optional spine for error context; defaults to
        formals_group[FORMALS_SOURCE_KEY].datum when present
    :return: (positional, defaults, kwonly, star, starstar, annotations)
    :raises SpecialError: on invalid structure
    """

    if source is None:
        sk = formals_group.get(FORMALS_SOURCE_KEY)
        source = sk.datum if sk else None

    annotations = {}
    positional = []
    defaults = []

    for fr in formals_group.get('positionals', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid positional frame {fr!r}",
                source)
        name, ann_bundle, default = _gather_one_positional_frame(fr, source)
        positional.append(name)
        _merge_formal_annotations(annotations, name, ann_bundle)
        if default is not None:
            defaults.append((keyword(str(name)), default))

    for fr in formals_group.get('defaults', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid defaults frame {fr!r}",
                source)
        kw = None if (v := fr.get('kw')) is None else v.datum
        dflt = None if (v := fr.get('default')) is None else v.datum
        if kw is None:
            raise SpecialError(
                f"defaults frame missing keyword: {fr!r}",
                source)
        defaults.append((kw, dflt))

    kwonly = []
    for fr in formals_group.get('kwonly', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid kwonly frame {fr!r}",
                source)
        name = None if (v := fr.get('name')) is None else v.datum
        if name is not None and (is_symbol(name) or is_lazygensym(name)):
            kwonly.append((keyword(str(name)), nil))
            continue
        kw = None if (v := fr.get('kw')) is None else v.datum
        dflt = None if (v := fr.get('default')) is None else v.datum
        if kw is not None:
            kwonly.append((kw, dflt))
        else:
            raise SpecialError(
                f"invalid kw-only frame {fr!r}",
                source)

    star_cell = formals_group.get('star')
    star = star_cell.datum if star_cell else None

    ss_cell = formals_group.get('starstar')
    starstar = ss_cell.datum if ss_cell else None
    if starstar and not is_symbol(starstar):
        raise SpecialError(
            f"** keyword requires symbol binding, not {starstar!r}",
            source)

    if star is not None and star is not nil:
        if not is_symbol(star):
            raise SpecialError(
                f"* keyword requires symbol binding, not {star!r}",
                source)

    return (positional, defaults, kwonly, star, starstar, annotations)


# The end.
