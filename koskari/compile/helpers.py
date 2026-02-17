"""
Helper functions for compiler specials

Parse and validate formals (definition-site), parameters (invocation-site),
and bindings (declare-and-bind, e.g. let). Validation is by raising during
parsing; all helpers raise CompilerException with optional source context.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from ..lib import car, cons, keyword, nil, OpCode, pair, symbol
from ..types import is_keyword, is_pair, is_proper, is_symbol, unpack

from .compiler import ByteCodeFragment
from .exceptions import CompilerException


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


def helper_binding(source, required=True):
    """
    Parse a single binding form (SYM EXPR) or (EXPR as: SYM).

    :param compiler: compiler instance (for error context)
    :param source: pair representing one binding
    :param required: if False and source has one element, treat as (EXPR) with
        binding name given by required (used for single-expr bindings)
    :return: (sym, expr) where sym is the bound name
    :raises CompilerException: when the binding form is invalid
    """

    msg = ("a binding must be in the form of (SYM EXPR) or (EXPR as: SYM),"
           " not %s" % source)

    if not is_proper(source):
        raise CompilerException(msg, source)

    parts = list(unpack(source))
    sc = len(parts)
    if sc == 2:
        first, second = parts[0], parts[1]
        if is_pair(second) and not is_symbolish(first):
            _as, sym_rest = second
            if is_keyword(_as) and str(_as) == "as":
                sym = car(sym_rest) if is_pair(sym_rest) else sym_rest
                if is_symbolish(sym):
                    return (sym, first)
        if is_symbolish(first):
            sym, expr = first, second
            return (sym, expr)
        raise CompilerException(msg, source)
    elif sc == 1 and not required:
        expr = parts[0]
        return (required, expr)
    else:
        raise CompilerException(msg, source)


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
    :raises CompilerException: on invalid parameter list
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
        raise CompilerException("parameters must be symbol or pair, not %r" % args, source)

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
            raise CompilerException("missing value for keyword parameter %s" % arg, source)
        else:
            defaults.append(value)

        arg = next(iargs, undefined)
        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            raise CompilerException("keyword parameters must be alternating keywords and"
                " values, not %r" % arg, source)

    star = None
    starstar = None

    if arg is undefined:
        return (positional, keywords, defaults, None, None)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            raise CompilerException("* keyword parameter needs value", source)
        arg = next(iargs, undefined)

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            raise CompilerException("** keyword parameter needs value", source)
        arg = next(iargs, undefined)

    if arg is not undefined:
        raise CompilerException("leftover parameters %r" % arg, source)

    return (positional, keywords, defaults, star, starstar)


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
    :raises CompilerException: on invalid formals list
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
        raise CompilerException("formals must be symbol or pair, not %r" % args, source)

    positional = []

    iargs = iter(unpack(args))
    for arg in iargs:
        if is_keyword(arg):
            if improper:
                raise CompilerException("cannot mix improper formal with keywords", source)
            else:
                break
        elif is_symbol(arg) or is_lazygensym(arg):
            positional.append(arg)
        else:
            raise CompilerException("positional formals must be symbols, not %r" % arg, source)
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
            raise CompilerException("missing value for keyword formal %s" % args, source)
        else:
            defaults.append((arg, value))

        arg = next(iargs, undefined)

        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            raise CompilerException("keyword formals must be alternating keywords and"
                " values, not %r" % arg)

    star = None
    starstar = None

    if arg is undefined:
        return (positional, defaults, kwonly, None, None)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            raise CompilerException("* keyword requires symbol binding", source)
        elif star is nil:
            pass
        elif not (is_symbol(star) or is_lazygensym(star)):
            raise CompilerException("* keyword requires symbol binding, not %r" % star, source)
        arg = next(iargs, undefined)

    if arg is undefined:
        return (positional, defaults, kwonly, star, starstar)

    if not is_keyword(arg):
        raise CompilerException("expected keyword in formals, got %r" % arg, source)

    while arg not in (_keyword_star, _keyword_starstar):
        value = next(iargs, undefined)
        if value is undefined:
            raise CompilerException("missing value for keyword-only formal %s" % arg, source)
        else:
            kwonly.append((arg, value))

        arg = next(iargs, undefined)
        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            raise CompilerException("keyword-only formals must be alternating keywords"
                " and values, not %r" % arg)

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            raise CompilerException("** keyword requires symbol binding", source)
        elif not (is_symbol(starstar) or is_lazygensym(starstar)):
            raise CompilerException("** keyword requires symbol binding, not %r" % star, source)
        arg = next(iargs, undefined)

    if arg is not undefined:
        raise CompilerException("leftover formals %r" % arg, source)

    return (positional, defaults, kwonly, star, starstar)


def simple_parameters(source_args, source=None):
    """
    Convenience view of gather_parameters: positional list and keyword dict.

    Callers that need star/starstar must use gather_parameters directly.

    :param source_args: same as gather_parameters args
    :param source: optional source form for error context
    :return: (pos_list, kw_dict) where kw_dict maps keyword name (str) to value expr
    :raises CompilerException: on invalid parameter list
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
    :raises CompilerException: on invalid or duplicate binding
    """

    names = []
    values = []
    if not is_pair(bindings_source):
        return (names, values)
    for elem in bindings_source.unpack():
        name, val = helper_binding(elem)
        if name in names:
            raise CompilerException(
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
    :raises CompilerException: on invalid or duplicate binding
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


__all__ = (
    "convert_bindings",
    "gather_bindings",
    "gather_formals",
    "gather_parameters",
    "helper_begin",
    "helper_binding",
    "is_lazygensym",
    "is_symbolish",
    "list_star",
    "pycons",
    "simple_parameters",
)


# The end.
