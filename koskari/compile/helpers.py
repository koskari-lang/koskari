"""
Helper functions for compiler specials

Parse and validate formals (definition-site), parameters (invocation-site),
and bindings (declare-and-bind, e.g. let). Validation is by raising during
parsing; all helpers raise SpecialError or ParametersError with optional source context.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from typing import Callable, Union, Tuple, List

from ..lib import car, cdr, cons, keyword, nil, OpCode, pair, symbol
from ..types import is_keyword, is_pair, is_proper, is_symbol, unpack

from .compiler import Compiler, Special, ByteCodeFragment
from ..exceptions import ParametersError, SpecialError
from ..types import get_position
from ..matcher import (
    VALUE,
    BindingsResult,
    BindingMatcher,
    BindingResult,
    FormalsResult,
    LITERAL_KW,
    LIST,
    MATCH,
    Matcher,
    PAIR,
    REST,
    SYMBOL,
    match1,
    matcher,
    UsingResult,
)


__all__ = (
    "BINDING",
    "BINDINGS",
    "FORMALS",
    "USING",
    "USING_UNPACK",
    "const_only_expr",
    "convert_bindings",
    "discover_docstring",
    "strip_leading_docstring",
    "gather_bindings",
    "gather_formals",
    "gather_parameters_ng",
    "helper_begin",
    "helper_binding",
    "helper_using",
    "using_unpack_helper",
    "parse_decl",
    "helper_elsebody",
    "is_lazygensym",
    "is_symbolish",
    "pycons",
    "requote",
    "special",
    "special_form",
)


SpecialFn = Callable[
    [Compiler, pair, bool],
    Union[ByteCodeFragment, pair, None]
]


def special_forms(
        patterns: List[Matcher]) -> Callable[[Special], Special]:
    """
    Purely informational decorator that adds patterns to a special form
    """

    def decorator(inst: Special) -> Special:
        inst.forms.extend(patterns)
        return inst
    return decorator


def special_form(
        pattern: Matcher) -> Callable[[Special], Special]:
    """
    Purely informational decorator that adds a pattern to a special form
    """

    def decorator(inst: Special) -> Special:
        inst.forms.append(pattern)
        return inst
    return decorator


def special(
        form_symbol: symbol,
        aliases: Tuple[symbol, ...] = ()) -> Special:
    """
    Decorator that builds a ``Special`` around the handler function.

    The decorated object gets a compile_impl attribute for dispatch.
    Publish names into the runtime builtins dict with
    ``koskari.stdlib.builtin`` as the outermost decorator on the stack.
    """

    def decorator(fn: SpecialFn):
        inst = Special(form_symbol, fn, aliases)
        return inst

    return decorator


# Types that pycons leaves as-is (atoms and pairs). Recurse only into tuple/list.
_PYCONS_DIRECT = (
    str, bytes, bool, int, float, complex,
    type(None), type(...),
    symbol, keyword, type(nil), pair,
)
_PYCONS_RECONS = (tuple, list)


_keyword_star = keyword("*")
_keyword_starstar = keyword("**")


def is_lazygensym(x):
    """
    Return True if x is a lazy gensym (compile-time placeholder).
    Koskari currently has no lazy gensyms; always False.
    """

    return False


def is_symbolish(thing):
    """
    Return True if thing is a symbol or lazy gensym (valid binding name)
    """

    return is_symbol(thing) or is_lazygensym(thing)


_tag_formals = symbol('matcher/formals')


@matcher(_tag_formals)
class FormalsMatcher(BindingMatcher):
    """
    Match formals (pair or symbol) and bind the result of gather_formals
    as FormalsResult (positional, defaults, kwonly, star, starstar,
    annotations on .annotations)
    """

    def __init__(self, slot, *, scope=None, scopes=None):
        self.slot = slot
        self._key = str(slot)
        self.scope = scope
        self.scopes = scopes


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('formals expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head = car(source)
        tail = cdr(source)
        if not (is_symbol(head) or is_pair(head)):
            return (None, False)
        try:
            pos, defaults, kwonly, star, starstar, ann = gather_formals(
                head, head)
        except SpecialError:
            return (None, False)
        result[self._key] = FormalsResult(
            head, get_position(source),
            positional=pos, defaults=defaults, kwonly=kwonly,
            star=star, starstar=starstar, annotations=ann)
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


    def extract_binder_symbols(self, value):
        if hasattr(value, 'positional'):
            positional = value.positional
            star = value.star
            starstar = value.starstar
        elif isinstance(value, (tuple, list)) and len(value) == 5:
            positional, _defaults, _kwonly, star, starstar = value
        else:
            return
        for s in (positional or ()):
            if is_symbol(s):
                yield s
        if star is not None and is_symbol(star):
            yield star
        if starstar is not None and is_symbol(starstar):
            yield starstar


    def get_recurse_targets(self, value):
        out = []
        if hasattr(value, 'defaults') and hasattr(value, 'kwonly'):
            for _kw, expr in (value.defaults or ()):
                out.append(expr)
            for _kw, expr in (value.kwonly or ()):
                out.append(expr)
            ann = getattr(value, 'annotations', None) or {}
            for bundle in ann.values():
                t = bundle.get('type')
                if t is not None:
                    out.append(t)
        elif isinstance(value, (tuple, list)) and len(value) == 5:
            _pos, defaults, kwonly, _star, _starstar = value
            for _kw, expr in (defaults or ()):
                out.append(expr)
            for _kw, expr in (kwonly or ()):
                out.append(expr)
        return out


    def to_tuple(self):
        return (_tag_formals, symbol(self._key) if isinstance(self.slot, str) else self.slot)


def FORMALS(slot, *, scope=None, scopes=None):
    """
    Match formals and bind FormalsResult (positional, defaults, kwonly, star,
    starstar) from gather_formals. Use .source for raw formals (pair or symbol).

    :param slot: capture key
    :param scope: single scope region key
    :param scopes: sequence of scope region keys
    """
    return FormalsMatcher(slot, scope=scope, scopes=scopes)


_tag_binding = symbol('matcher/binding')
_tag_using = symbol('matcher/using')
_tag_using_unpack = symbol('matcher/using-unpack')


_BINDING_FORMS = [
    LIST(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    LIST(SYMBOL("name"), VALUE("expr")),
]
_BINDING_FORMS_ALLOW_UNNAMED = [
    *_BINDING_FORMS,
    LIST(VALUE("expr")),
]

# Binding forms before LIST(VALUE) so (EXPR as: SYM) wins over unnamed (EXPR)
_USING_FORMS = (
    *_BINDING_FORMS,
    LIST(VALUE("expr")),
)

_USING_UNPACK_FORMS = (
    LIST(PAIR("lhs"), VALUE("expr")),
)


@matcher(_tag_binding)
class BindingMatcher(BindingMatcher):
    """
    Match a single binding form (SYM EXPR) or (EXPR as: SYM).
    """

    def __init__(self, slot, *, scope=None, scopes=None):
        self.slot = slot
        self._key = str(slot)
        self.scope = scope
        self.scopes = scopes


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('binding expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def extract_binder_symbols(self, value):
        if hasattr(value, 'name') and value.name is not None and is_symbol(value.name):
            yield value.name


    def get_recurse_targets(self, value):
        if isinstance(value, BindingResult) and value.value is not None:
            return [value.value]
        return ()


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)

        head, remainder = source
        if not is_pair(head):
            return (None, False)

        m = match1(head, _BINDING_FORMS)
        if m is None:
            return (None, False)

        name = m.get("name")
        if name:
            name = name.source
        value = m.get("expr").source

        result[self._key] = BindingResult(
            head, get_position(source), name=name, value=value)

        if specs is not None:
            specs[self._key] = self

        return (remainder, True)


    def to_tuple(self):
        return (_tag_binding, symbol(self._key) if isinstance(self.slot, str)
                else self.slot)


def BINDING(slot, *, scope=None, scopes=None):
    """
    Match a single binding form (SYM EXPR) or (EXPR as: SYM).

    :param slot: capture key
    :param scope: single scope region key
    :param scopes: sequence of scope region keys
    """
    return BindingMatcher(slot, scope=scope, scopes=scopes)


@matcher(_tag_using)
class UsingMatcher(BindingMatcher):
    """
    Match a single using form (EXPR), (EXPR as: SYM), or (SYM EXPR).
    """

    def __init__(self, slot, *, scope=None, scopes=None):
        self.slot = slot
        self._key = str(slot)
        self.scope = scope
        self.scopes = scopes

    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('using expects 1 payload, got %d' % len(payload))
        return cls(payload[0])

    def extract_binder_symbols(self, value):
        if hasattr(value, 'name') and value.name is not None \
                and is_symbolish(value.name):
            yield value.name

    def get_recurse_targets(self, value):
        if isinstance(value, BindingResult) and value.value is not None:
            return [value.value]
        return ()

    def matches_element(self, value, literals, result, specs=None, source=None):
        return False

    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)

        head, remainder = source
        if not is_pair(head):
            return (None, False)

        m = match1(head, _USING_FORMS)
        if m is None:
            return (None, False)

        name_m = m.get("name")
        expr_m = m.get("expr")
        value = expr_m.source
        name = name_m.source if name_m else None

        result[self._key] = UsingResult(
            head, get_position(source), name=name, value=value, lhs=None)

        if specs is not None:
            specs[self._key] = self

        return (remainder, True)

    def to_tuple(self):
        return (_tag_using, symbol(self._key) if isinstance(self.slot, str)
                else self.slot)


def USING(slot, *, scope=None, scopes=None):
    """
    Match a single using form (EXPR), (EXPR as: NAME), or (NAME EXPR).
    """
    return UsingMatcher(slot, scope=scope, scopes=scopes)


@matcher(_tag_using_unpack)
class UsingUnpackMatcher(BindingMatcher):
    """
    Match ((PATTERN...) EXPR) only; for compile/for-each unpack binding.
    """

    def __init__(self, slot, *, scope=None, scopes=None):
        self.slot = slot
        self._key = str(slot)
        self.scope = scope
        self.scopes = scopes

    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError(
                'using-unpack expects 1 payload, got %d' % len(payload))
        return cls(payload[0])

    def extract_binder_symbols(self, value):
        yield from unpack_lhs_bind_symbols(value.lhs, value.source, flatten=True)

    def get_recurse_targets(self, value):
        if isinstance(value, BindingResult) and value.value is not None:
            return [value.value]
        return ()

    def matches_element(self, value, literals, result, specs=None, source=None):
        return False

    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)

        head, remainder = source
        if not is_pair(head):
            return (None, False)

        m = match1(head, _USING_UNPACK_FORMS)
        if m is None:
            return (None, False)

        lhs_m = m.get("lhs")
        expr_m = m.get("expr")
        result[self._key] = UsingResult(
            head,
            get_position(source),
            name=None,
            value=expr_m.source,
            lhs=lhs_m.source)

        if specs is not None:
            specs[self._key] = self

        return (remainder, True)

    def to_tuple(self):
        return (_tag_using_unpack, symbol(self._key) if isinstance(
            self.slot, str) else self.slot)


def USING_UNPACK(slot, *, scope=None, scopes=None):
    """
    Match ((PATTERN...) EXPR) for unpack patterns (e.g. for-each).
    """
    return UsingUnpackMatcher(slot, scope=scope, scopes=scopes)


def helper_binding(source):
    """
    Parse a single binding form (SYM EXPR) or (EXPR as: SYM).

    :param source: pair representing one binding
    :return: (name, expr) where name is the bound name
    :raises SpecialError: when the binding form is invalid
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

    return (name.source if name else None, value.source)


def helper_using(source):
    """
    Parse a single using form (EXPR), (EXPR as: SYM), or (SYM EXPR).

    :param source: pair representing one using
    :return: (name, expr) where name is the bound symbol or None, and expr is
        the value form
    :raises SpecialError: when the using form is invalid
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

    name = name_m.source if name_m else None

    return (name, value.source)


def unpack_lhs_bind_symbols(lhs, source=None, flatten=False):
    """
    Yield each symbol introduced by an unpack LHS (same shapes as
    using_unpack_helper)

    :param lhs: bare symbol or proper list pattern with optional *
    :param source: optional form for SpecialError context
    :param flatten: if True, flatten the list of symbols into a single list
    :yields: each bound symbol in VAR_STORE order
    :raises SpecialError: when lhs is not a valid unpack pattern
    """

    ctx = source if source is not None else lhs

    def fail(msg):
        raise SpecialError(msg, ctx)

    if is_symbolish(lhs):
        yield lhs
        return

    if not is_proper(lhs):
        fail('unpack pattern must be a symbol or a proper list')

    items = list(unpack(lhs))
    if not items:
        fail('empty unpack pattern')

    star_idx = [
        i for i, elt in enumerate(items)
        if is_keyword(elt) and elt is _keyword_star]

    if len(star_idx) > 1:
        fail('at most one * is allowed in an unpack pattern')

    if not star_idx:
        for elt in items:
            if flatten and is_pair(elt):
                yield from unpack_lhs_bind_symbols(elt, source, flatten)
            elif not is_symbolish(elt):
                fail('unpack pattern list elements must be symbols')
            else:
                yield elt
        return

    si = star_idx[0]
    prefix = items[:si]
    after_star = items[si + 1:]
    if not after_star:
        fail('* in unpack pattern must be followed by a binding name')

    star_name = after_star[0]
    suffix = after_star[1:]
    if not is_symbolish(star_name):
        fail('name after * in unpack pattern must be a symbol')

    for elt in prefix:
        if flatten and is_pair(elt):
            yield from unpack_lhs_bind_symbols(elt, source, flatten)
        elif not is_symbolish(elt):
            fail('unpack pattern list elements must be symbols')
        else:
            yield elt

    yield star_name

    for elt in suffix:
        if flatten and is_pair(elt):
            yield from unpack_lhs_bind_symbols(elt, source, flatten)
        elif not is_symbolish(elt):
            fail('unpack pattern list elements must be symbols')
        else:
            yield elt


def using_unpack_helper(compiler, lhs, source=None, declare=True):
    """
    Build bytecode to bind names from TOS without compiling an RHS

    Caller must leave the sequence value on the stack (TOS). The returned
    fragment records declare_var for each bound name when declare is True
    and emits UNPACK, UNPACK_STAR, and VAR_STORE as needed. It never calls
    full_compile

    :param compiler: active Compiler
    :param lhs: bare symbol, or proper list of symbols with optional one *
    :param source: optional form for SpecialError context
    :param declare: when False, omit declare_var (assignment to existing names)
    :return: ByteCodeFragment with declares and store instructions
    :raises SpecialError: when lhs is not a valid unpack pattern
    """

    ctx = source if source is not None else lhs

    def fail(msg):
        raise SpecialError(msg, ctx)

    def maybe_declare(var_name):
        if declare:
            frag.declare_var(var_name)

    frag = compiler.fragment('using.unpack')
    if source is not None:
        frag.position_of(source)

    if is_symbolish(lhs):
        name = str(lhs)
        maybe_declare(name)
        frag.instruction(OpCode.KOS_OP_VAR_STORE, name)
        return frag

    if not is_proper(lhs):
        fail('unpack pattern must be a symbol or a proper list')

    items = list(unpack(lhs))
    if not items:
        fail('empty unpack pattern')

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
        fail('at most one * is allowed in an unpack pattern')

    # no star, so we just unpack the items
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
                fail('unpack pattern list elements must be symbols or pairs')
        return frag

    # star, so we'll need to prefix and then star, and then suffix
    si = star_idx[0]
    prefix = items[:si]
    after_star = items[si + 1:]
    if not after_star:
        fail('* in unpack pattern must be followed by a binding name')

    star_name = after_star[0]
    suffix = after_star[1:]
    if not is_symbolish(star_name):
        fail('name after * in unpack pattern must be a symbol')
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
            fail('unpack pattern list elements must be symbols or pairs')

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
            fail('unpack pattern list elements must be symbols or pairs')

    return frag


_tag_bindings = symbol('matcher/bind-bindings')


@matcher(_tag_bindings)
class _BindingsMatcher(BindingMatcher):
    """
    Match a list and bind the result of gather_bindings as (names, values)
    """

    def __init__(self, slot, mode='parallel', scope=None, scopes=None):
        self.slot = slot
        self._key = str(slot)
        self.mode = mode
        self.scope = scope
        self.scopes = scopes


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('bind-bindings expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)

        head, tail = source
        if not is_pair(head):
            return (None, False)

        names, values = gather_bindings(head, head)
        result[self._key] = BindingsResult(
            head, get_position(source), names=names, values=values)
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


    def extract_binder_symbols(self, value):
        if hasattr(value, 'names'):
            names = value.names
        elif isinstance(value, (tuple, list)) and len(value) == 2:
            names, _values = value
        else:
            return
        for n in (names or ()):
            if is_symbol(n):
                yield n


    def get_recurse_targets(self, value):
        if hasattr(value, 'values'):
            return value.values or ()
        if isinstance(value, (tuple, list)) and len(value) == 2:
            return value[1] or ()
        return ()


    def to_tuple(self):
        return (_tag_bindings, symbol(self._key) if isinstance(self.slot, str) else self.slot)


def BINDINGS(slot, *, mode='parallel', scope=None, scopes=None):
    """
    Match a list and bind (names, values) from gather_bindings

    :param slot: capture key
    :param mode: parallel, sequential, or recursive
    :param scope: single scope region key
    :param scopes: sequence of scope region keys
    """
    return _BindingsMatcher(slot, mode=mode, scope=scope, scopes=scopes)


def gather_parameters_ng(args, source=None):
    """
    Parse call-site argument list into interleaved args_seq and kwds_seq.

    Supports Python 3.5+ semantics: positionals interleave with star-args;
    keywords interleave with starstar-args. All positionals and * must come
    before any keywords or **; a keyword cannot be followed by * or positional.

    :param args: pair, improper list, or single symbol (rest arg)
    :param source: optional source form for error context
    :return: (args_seq, kwds_seq)
        - args_seq: list of (index, expr) for positionals or ("*", symbol)
          for star-args
        - kwds_seq: list of (keyword_name, expr) for keywords or ("**", symbol)
          for starstar-args
    :raises ParametersError: on invalid parameter list
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


def _formal_kw_tag_to_key(kw):
    """
    Map keyword atom to annotation key; empty keyword (``::`` sugar) is type
    """

    raw = str(kw)
    if raw.endswith(':'):
        raw = raw[:-1]
    if raw == '':
        return 'type'
    return raw


def _parse_formal_tail(tail, source):
    """
    Parse REST tail after formal name: keyword/value pairs or lone default.

    :return: (ann_bundle, default_expr) where ann_bundle may include type
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
        if key == 'value':
            default = val
        elif key == 'type':
            ann['type'] = val
        else:
            raise SpecialError(
                "unknown formal attribute %r" % head,
                source)
    return ann, default


def parse_formal_parameter(form, source=None):
    """
    Parse one formal parameter: bare symbol, or list with name and attributes.

    :return: (name_symbol, annotation_bundle, default_expr)
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
        return (m_sv["name"].source, {}, m_sv["expr"].source)

    _expr_as_name = (
        LIST(VALUE("type_expr"), LITERAL_KW("as"), SYMBOL("name")),
    )
    m_as = match1(form, _expr_as_name)
    if m_as is not None:
        return (
            m_as["name"].source,
            {"type": m_as["type_expr"].source},
            None)

    _FORMAL_HEAD = (LIST(MATCH(SYMBOL("name"), REST("tail"))),)
    m = match1(form, _FORMAL_HEAD)
    if m is None:
        raise SpecialError("invalid formal parameter form %r" % form, source)

    name = m["name"].source
    tail = m["tail"].source
    ann, default = _parse_formal_tail(tail, source)
    return (name, ann, default)


def _parse_decl_tail(tail, source):
    """
    Parse REST after a declaration name: only keyword attributes, no value
    position and no value:.

    :return: annotation bundle (may include type)
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
        if key == 'value':
            raise SpecialError(
                "value: is not allowed in declaration",
                source)
        elif key == 'type':
            ann['type'] = val
        else:
            raise SpecialError(
                "unknown declaration attribute %r" % kw,
                source)
    return ann


def parse_decl(form, source=None):
    """
    Parse a binding-shaped declaration with no value: bare symbol, or list
    with name and optional type attributes (``::`` / ``type:``), or
    ``(TYPE as: NAME)``.

    :return: (name_symbol, type_expr_or_None)
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
        return (m_as["name"].source, m_as["type_expr"].source)

    _FORMAL_HEAD = (LIST(MATCH(SYMBOL("name"), REST("tail"))),)
    m = match1(form, _FORMAL_HEAD)
    if m is None:
        raise SpecialError("invalid declaration form %r" % form, source)

    name = m["name"].source
    tail = m["tail"].source
    ann = _parse_decl_tail(tail, source)
    type_expr = ann.get('type')
    return (name, type_expr)


def _merge_formal_annotations(annotations, name_sym, ann_bundle):
    """
    Merge ann_bundle into annotations dict keyed by str(name_sym).
    """

    if not ann_bundle:
        return
    key = str(name_sym)
    if key in annotations:
        annotations[key].update(ann_bundle)
    else:
        annotations[key] = dict(ann_bundle)


def gather_formals(args, source=None):
    """
    Parse definition-site formals into six values.

    :param args: pair, improper list, or single symbol (rest arg)
    :param source: optional source form for error context
    :return: (positional, defaults, kwonly, star, starstar, annotations)
        - positional: list of symbols for positional parameters
        - defaults: list of (keyword, default_expr) for keyword params
        - kwonly: list of (keyword, default_expr) for keyword-only params
          (after ``*:`` a bare symbol means required keyword-only, no default)
        - star: symbol or None for *args
        - starstar: symbol or None for **kwargs
        - annotations: dict name str -> {type: expr, ...} per parameter
    :raises SpecialError: on invalid formals list
    """

    undefined = object()
    annotations = {}

    if is_symbol(args):
        return ([], [], [], args, None, annotations)

    elif isinstance(args, (list, tuple)):
        improper = False
        args = cons(*args, nil)

    elif is_proper(args):
        improper = False

    elif is_pair(args):
        improper = True

    else:
        raise SpecialError("formals must be symbol or pair, not %r" % args, source)

    positional = []
    defaults = []

    iargs = iter(unpack(args))
    for arg in iargs:
        if is_keyword(arg):
            if improper:
                raise SpecialError("cannot mix improper formal with keywords", source)
            else:
                break
        elif is_symbol(arg) or is_lazygensym(arg):
            positional.append(arg)
        else:
            try:
                name, ann, default = parse_formal_parameter(arg, source)
            except SpecialError as err:
                raise SpecialError(
                    "positional formals must be symbols or bracket forms, not %r"
                    % arg,
                    source) from err
            positional.append(name)
            _merge_formal_annotations(annotations, name, ann)
            if default is not None:
                defaults.append((keyword(str(name)), default))
    else:
        if improper:
            return (positional[:-1], defaults, [], positional[-1], None, annotations)
        else:
            return (positional, defaults, [], None, None, annotations)

    kwonly = []

    while arg not in (_keyword_star, _keyword_starstar):
        value = next(iargs, undefined)
        if value is undefined:
            raise SpecialError("missing value for keyword formal %s" % args, source)
        else:
            defaults.append((arg, value))

        arg = next(iargs, undefined)

        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            raise SpecialError(
                "keyword formals must be alternating keywords and"
                " values, not %r" % arg,
                source)

    star = None
    starstar = None

    if arg is undefined:
        return (positional, defaults, kwonly, None, None, annotations)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            raise SpecialError("* keyword requires symbol binding", source)
        elif star is nil:
            pass
        elif not (is_symbol(star) or is_lazygensym(star)):
            raise SpecialError(f"* keyword requires symbol binding, not {star!r}", source)
        arg = next(iargs, undefined)

    if arg is undefined:
        return (positional, defaults, kwonly, star, starstar, annotations)

    while arg not in (_keyword_star, _keyword_starstar):
        if is_symbol(arg) or is_lazygensym(arg):
            kwonly.append((keyword(str(arg)), nil))
            arg = next(iargs, undefined)
            if arg is undefined:
                break
            continue

        if not is_keyword(arg):
            raise SpecialError("expected keyword in formals, got %r" % arg, source)

        value = next(iargs, undefined)
        if value is undefined:
            raise SpecialError(f"missing value for keyword-only formal {arg}", source)
        else:
            kwonly.append((arg, value))

        arg = next(iargs, undefined)
        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            raise SpecialError(
                "keyword-only formals must be alternating keywords"
                f" and values, not {arg!r}")

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            raise SpecialError("** keyword requires symbol binding", source)
        elif not (is_symbol(starstar) or is_lazygensym(starstar)):
            raise SpecialError(f"** keyword requires symbol binding, not {starstar!r}", source)
        arg = next(iargs, undefined)

    if arg is not undefined:
        raise SpecialError("leftover formals %r" % arg, source)

    return (positional, defaults, kwonly, star, starstar, annotations)


def gather_bindings(bindings_source, source=None):
    """
    Parse a list of binding forms into parallel names and value expressions.

    Each element of bindings_source must be a valid binding (SYM EXPR) or
    (EXPR as: SYM). Duplicate names raise. Validation is by raising during
    iteration.

    :param bindings_source: proper list of binding forms, e.g. [[a 1] [b 2]]
    :param source: optional source form for error context
    :return: (names, values) — list of name symbols and list of expressions
    :raises SpecialError: on invalid or duplicate binding
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
    Turn a list of binding forms into a keyword/value proper list.

    Used when a construct needs formals-with-defaults as a single list
    (e.g. future letrec or macro building a lambda from bindings).
    Each binding is parsed with helper_binding; result is alternating
    keyword(name) and value expression, e.g. (keyword("a"), 1, keyword("b"), 2, nil).

    :param bindings_source: proper list of binding forms
    :param source: optional source form for error context
    :return: proper list of (keyword(name), val, keyword(name), val, ...)
    :raises SpecialError: on invalid or duplicate binding
    """

    names, values = gather_bindings(bindings_source, source)
    col = []
    for n, v in zip(names, values):
        col.append(keyword(n))
        col.append(v)
    return cons(*col, nil) if col else nil


def pycons(val):
    """
    Turn a Python tuple or list into a proper list (nested pairs ending in nil).
    Atoms and pairs are returned unchanged. Recurses into tuples and lists only.

    :param val: a tuple, list, pair, or atom (symbol, keyword, number, etc.)
    :return: a proper list when val is tuple/list; otherwise val unchanged
    :raises TypeError: when val is not a supported type
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
    Check whether a ByteCodeFragment consists only of a single CONST value.

    Ignores NOOP and POSITION; allows a single CONST optionally followed by
    POP 1. Used by item specials to choose CONST_ITEM* vs ITEM* opcodes.

    :param fragment: ByteCodeFragment to inspect
    :return: (found, value) — found is True iff exactly one CONST; value is
        the constant or None
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
    Rewrite a list form so each element is wrapped in (quote_sym ELEMENT).
    Builds (cons_sym (quote e1) (quote e2) ...). Used by compile/quote when
    the quoted form is a pair. No runtime cons: expansion uses compile/cons.

    :param source: proper or improper list (pair) to walk
    :param cons_sym: symbol for the list constructor (default symbol("compile/cons"))
    :param quote_sym: symbol for each quoted wrapper (default symbol("compile/quote"))
    :return: pair (cons_sym . ((quote e1) . ((quote e2) . ... nil)))
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
            # end of proper list
            col.append(nil)
            break
        elif not is_pair(rest):
            # end of improper list
            col.append(cons(quote_sym, rest, nil))
            break

        # continue with next element
        source = rest

    return cons(cons_sym, *col, nil)


_SYM_BEGIN = symbol('begin')
_SYM_COMPILE_BEGIN = symbol('compile/begin')
_BEGIN_HEADS = (_SYM_BEGIN, _SYM_COMPILE_BEGIN)


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


def _strip_leading_docstring_body(body):
    """
    Return (docstring, new_body) after removing one leading docstring per
    discover_docstring rules, or (None, None) if there is no such docstring.

    :param body: proper list of forms (pair or nil)
    :return: (str, pair or nil) or (None, None)
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
    If the leading form(s) are a docstring per discover_docstring, return
    (docstring, body with that prefix removed). Otherwise return (None, body).

    Does not mutate body.

    :param body: proper list of forms (pair or nil)
    :return: (str or None, body list to compile)
    """

    doc, new_body = _strip_leading_docstring_body(body)
    if doc is None:
        return None, body
    return doc, new_body


def helper_begin(
        fragment: ByteCodeFragment,
        body: pair,
        tc: bool = False) -> ByteCodeFragment:

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


_keyword_else = keyword("else")


def helper_elsebody(source):
    """
    Strip a trailing [else: EXPR] clause from a body list

    Returns a tuple of (body_list, else_expr_or_nil) where body_list is a pair
    list of all body expressions before the optional else clause.
    If no else clause is present, else_expr_or_nil is nil.
    If the else clause is empty, else_expr_or_nil is None.
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


# The end.
