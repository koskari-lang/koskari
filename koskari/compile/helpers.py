"""
Helper functions for compiler specials

Parse and validate formals (definition-site), parameters (invocation-site),
and bindings (declare-and-bind, e.g. let). Validation is by raising during
parsing; all helpers raise SpecialError or ParametersError with optional source context.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from typing import Callable, Union, Dict, Tuple, List

from ..lib import car, cdr, cons, keyword, nil, OpCode, pair, symbol
from ..types import is_keyword, is_pair, is_proper, is_symbol, unpack

from .compiler import Compiler, Special, ByteCodeFragment
from ..exceptions import ParametersError, SpecialError
from ..matcher import (
    BIND,
    LITERAL,
    LITERAL_KW,
    MATCH,
    Matcher,
    SYMBOL,
    match,
    matcher,
)


__all__ = (
    "BIND_BINDINGS",
    "BINDINGS",
    "const_only_expr",
    "convert_bindings",
    "gather_bindings",
    "gather_formals",
    "gather_parameters_ng",
    "gather_parameters",
    "helper_begin",
    "helper_binding",
    "helper_elsebody",
    "is_lazygensym",
    "is_symbolish",
    "list_star",
    "pycons",
    "requote",
    "simple_parameters",
    "special",
    "special_form",
    "SPECIALS",
)


SpecialFn = Callable[
    [Compiler, pair, bool],
    Union[ByteCodeFragment, pair, None]
]


SPECIALS: Dict[symbol, SpecialFn] = {}


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
    Decorator that registers a function as a special form handler.
    The decorated function gets a compile_impl attribute for dispatch.
    """

    def decorator(fn: SpecialFn):
        inst = Special(form_symbol, fn, aliases)
        SPECIALS[form_symbol] = inst
        for alias in aliases:
            SPECIALS[alias] = inst
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


_tag_bindings = symbol('matcher/bind-bindings')


@matcher(_tag_bindings)
class _BindingsMatcher:
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


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head = car(source)
        tail = cdr(source)
        if not is_pair(head):
            return (None, False)
        names, values = gather_bindings(head, head)
        result[self._key] = (names, values)
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


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


def BIND_BINDINGS(slot):
    """
    Deprecated alias for BINDINGS(slot). Use BINDINGS(slot) instead.
    """
    return BINDINGS(slot)


_BINDING_PATTERNS_3 = [
    MATCH(BIND("expr"), LITERAL_KW("as"), SYMBOL("sym")),
]
_BINDING_PATTERNS_2 = [
    MATCH(BIND("expr"), MATCH(LITERAL_KW("as"), MATCH(SYMBOL("sym"), LITERAL(nil)))),
    MATCH(SYMBOL("sym"), BIND("expr")),
]
_BINDING_PATTERN_1 = MATCH(BIND("expr"))


def helper_binding(source, required=True, allow_unnamed=False):
    """
    Parse a single binding form (SYM EXPR), (EXPR as: SYM), or when
    allow_unnamed True then (EXPR) alone.

    :param source: pair representing one binding
    :param required: if False and source has one element (and not allow_unnamed),
        treat as (EXPR) with binding name given by required
    :param allow_unnamed: if True, a single element (EXPR) is allowed and
        returns (None, expr)
    :return: (name, expr) where name is the bound name or None if unnamed
    :raises SpecialError: when the binding form is invalid
    """

    msg = ("a binding must be in the form of (SYM EXPR) or (EXPR as: SYM),"
           " not %s" % source)

    if not is_proper(source):
        raise SpecialError(msg, source)

    patterns = list(_BINDING_PATTERNS_3) + list(_BINDING_PATTERNS_2)
    if allow_unnamed or not required:
        patterns.append(_BINDING_PATTERN_1)

    m = match(source, patterns)
    if m is None:
        raise SpecialError(msg, source)

    if "sym" in m and "expr" in m:
        return (m["sym"], m["expr"])

    if "expr" in m:
        if allow_unnamed:
            return (None, m["expr"])
        return (required, m["expr"])

    raise SpecialError(msg, source)


def gather_parameters(args, source=None):
    """
    Parse call-site argument list into five values.

    :param args: pair, improper list, or single symbol (rest arg)
    :param source: optional source form for error context
    :return: (positional, keywords, values, stararg, starstararg)
        - positional: list of expressions for positional arguments
        - keywords: list of keyword objects for keyword argument names
        - values: list of expressions (one per keyword)
        - stararg: symbol or None for *args
        - starstararg: symbol or None for **kwargs
    :raises ParametersError: on invalid parameter list
    """

    undefined = object()

    if is_symbol(args) or is_lazygensym(args):
        return ([], [], [], args, None)

    elif isinstance(args, (list, tuple)):
        improper = False
        args = cons(*args, nil) if args else nil

    elif is_proper(args):
        improper = False

    elif is_pair(args):
        improper = True

    else:
        raise ParametersError("parameters must be symbol or pair, not %r" % args, source)

    positional = []

    iargs = iter(unpack(args))
    for arg in iargs:
        if is_keyword(arg):
            break
        else:
            positional.append(arg)
    else:
        if improper:
            return (positional[:-1], [], [], positional[-1], None)
        else:
            return (positional, [], [], None, None)

    keywords = []
    defaults = []

    while arg not in (_keyword_star, _keyword_starstar):
        keywords.append(arg)

        value = next(iargs, undefined)
        if value is undefined:
            raise ParametersError("missing value for keyword parameter %s" % arg, source)
        else:
            defaults.append(value)

        arg = next(iargs, undefined)
        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            raise ParametersError(
                "keyword parameters must be alternating keywords and"
                " values, not %r" % arg,
                source)

    star = None
    starstar = None

    if arg is undefined:
        return (positional, keywords, defaults, None, None)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            raise ParametersError("* keyword parameter needs value", source)
        arg = next(iargs, undefined)

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            raise ParametersError("** keyword parameter needs value", source)
        arg = next(iargs, undefined)

    if arg is not undefined:
        raise ParametersError("leftover parameters %r" % arg, source)

    return (positional, keywords, defaults, star, starstar)


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


def gather_formals(args, source=None):
    """
    Parse definition-site formals into five values.

    :param args: pair, improper list, or single symbol (rest arg)
    :param source: optional source form for error context
    :return: (positional, defaults, kwonly, star, starstar)
        - positional: list of symbols for positional parameters
        - defaults: list of (keyword, default_expr) for keyword params
        - kwonly: list of (keyword, default_expr) for keyword-only params
        - star: symbol or None for *args
        - starstar: symbol or None for **kwargs
    :raises SpecialError: on invalid formals list
    """

    undefined = object()

    if is_symbol(args):
        return ([], [], [], args, None)

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
            raise SpecialError("positional formals must be symbols, not %r" % arg, source)
    else:
        if improper:
            return (positional[:-1], [], [], positional[-1], None)
        else:
            return (positional, [], [], None, None)

    defaults = []
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
        return (positional, defaults, kwonly, None, None)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            raise SpecialError("* keyword requires symbol binding", source)
        elif star is nil:
            pass
        elif not (is_symbol(star) or is_lazygensym(star)):
            raise SpecialError("* keyword requires symbol binding, not %r" % star, source)
        arg = next(iargs, undefined)

    if arg is undefined:
        return (positional, defaults, kwonly, star, starstar)

    if not is_keyword(arg):
        raise SpecialError("expected keyword in formals, got %r" % arg, source)

    while arg not in (_keyword_star, _keyword_starstar):
        value = next(iargs, undefined)
        if value is undefined:
            raise SpecialError("missing value for keyword-only formal %s" % arg, source)
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
                " and values, not %r" % arg)

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            raise SpecialError("** keyword requires symbol binding", source)
        elif not (is_symbol(starstar) or is_lazygensym(starstar)):
            raise SpecialError("** keyword requires symbol binding, not %r" % starstar, source)
        arg = next(iargs, undefined)

    if arg is not undefined:
        raise SpecialError("leftover formals %r" % arg, source)

    return (positional, defaults, kwonly, star, starstar)


def simple_parameters(source_args, source=None):
    """
    Convenience view of gather_parameters: positional list and keyword dict.

    Callers that need star/starstar must use gather_parameters directly.

    :param source_args: same as gather_parameters args
    :param source: optional source form for error context
    :return: (pos_list, kw_dict) where kw_dict maps keyword name (str) to value expr
    :raises ParametersError: on invalid parameter list
    """

    pos, keywords, values, star, starstar = gather_parameters(
        source_args, source)
    kw_dict = {} if not keywords else dict(
        (k, v) for k, v in zip(keywords, values))
    return (pos, kw_dict)


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
                "duplicate declarations for %s in bindings" % name,
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
    raise TypeError("cannot pycons %r" % (val,))


def list_star(*args):
    """
    Build a chain of pairs (a . (b . (c . tail))) from list_star(a, b, c, tail).
    Variadic: the last argument is the final cdr; all others are elements.
    Requires at least one argument.

    :param args: one or more values; the last is the tail of the result list
    :return: (args[0] . (args[1] . ... . (args[-2] . args[-1])))
    """

    if not args:
        raise TypeError("list_star requires at least one argument")
    tail = args[-1]
    for elem in reversed(args[:-1]):
        tail = pair(elem, tail)
    return tail


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


def requote(source,
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
    while source:
        element, source = source
        elem = cons(quote_sym, element, nil)
        col.append(elem)

    return list_star(cons_sym, *col, nil)


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
