"""
Helper functions for compiler specials

Parameter and formals parsing shared by call, lambda, function, and other
specials. Ported from python-sibilant; uses Koskari types and
KoskariSyntaxError.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from .compiler import ByteCodeFragment
from ..exceptions import KoskariSyntaxError
from ..lib import cons, keyword, nil, OpCode, pair
from ..types import is_keyword, is_pair, is_proper, is_symbol, unpack


_keyword_star = keyword("*")
_keyword_starstar = keyword("**")


def is_lazygensym(x):
    """
    Return True if x is a lazy gensym (compile-time placeholder).
    Koskari currently has no lazy gensyms; always False.
    """

    return False


def gather_parameters(args, declared_at=None, filename=None):
    """
    Parse call-site argument list into five values.

    :param args: pair, improper list, or single symbol (rest arg)
    :param declared_at: optional source location for error messages
    :param filename: optional filename for error messages
    :return: (positional, keywords, values, stararg, starstararg)
        - positional: list of expressions for positional arguments
        - keywords: list of keyword objects for keyword argument names
        - values: list of expressions (one per keyword)
        - stararg: symbol or None for *args
        - starstararg: symbol or None for **kwargs
    :raises KoskariSyntaxError: on invalid parameter list
    """

    undefined = object()

    def err(msg):
        raise KoskariSyntaxError(msg, location=declared_at, filename=filename)

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
        err("parameters must be symbol or pair, not %r" % args)

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
            err("missing value for keyword parameter %s" % arg)
        else:
            defaults.append(value)

        arg = next(iargs, undefined)
        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            err("keyword parameters must be alternating keywords and"
                " values, not %r" % arg)

    star = None
    starstar = None

    if arg is undefined:
        return (positional, keywords, defaults, None, None)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            err("* keyword parameter needs value")
        arg = next(iargs, undefined)

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            err("** keyword parameter needs value")
        arg = next(iargs, undefined)

    if arg is not undefined:
        err("leftover parameters %r" % arg)

    return (positional, keywords, defaults, star, starstar)


def gather_formals(args, declared_at=None, filename=None):
    """
    Parse definition-site formals into five values.

    :param args: pair, improper list, or single symbol (rest arg)
    :param declared_at: optional source location for error messages
    :param filename: optional filename for error messages
    :return: (positional, defaults, kwonly, star, starstar)
        - positional: list of symbols for positional parameters
        - defaults: list of (keyword, default_expr) for keyword params
        - kwonly: list of (keyword, default_expr) for keyword-only params
        - star: symbol or None for *args
        - starstar: symbol or None for **kwargs
    :raises KoskariSyntaxError: on invalid formals list
    """

    undefined = object()

    def err(msg):
        raise KoskariSyntaxError(msg, location=declared_at, filename=filename)

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
        err("formals must be symbol or pair, not %r" % args)

    positional = []

    iargs = iter(unpack(args))
    for arg in iargs:
        if is_keyword(arg):
            if improper:
                err("cannot mix improper formal with keywords")
            else:
                break
        elif is_symbol(arg) or is_lazygensym(arg):
            positional.append(arg)
        else:
            err("positional formals must be symbols, not %r" % arg)
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
            err("missing value for keyword formal %s" % args)
        else:
            defaults.append((arg, value))

        arg = next(iargs, undefined)

        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            err("keyword formals must be alternating keywords and"
                " values, not %r" % arg)

    star = None
    starstar = None

    if arg is undefined:
        return (positional, defaults, kwonly, None, None)

    if arg is _keyword_star:
        star = next(iargs, undefined)
        if star is undefined:
            err("* keyword requires symbol binding")
        elif star is nil:
            pass
        elif not (is_symbol(star) or is_lazygensym(star)):
            err("* keyword requires symbol binding, not %r" % star)
        arg = next(iargs, undefined)

    if arg is undefined:
        return (positional, defaults, kwonly, star, starstar)

    if not is_keyword(arg):
        err("expected keyword in formals, got %r" % arg)

    while arg not in (_keyword_star, _keyword_starstar):
        value = next(iargs, undefined)
        if value is undefined:
            err("missing value for keyword-only formal %s" % arg)
        else:
            kwonly.append((arg, value))

        arg = next(iargs, undefined)
        if arg is undefined:
            break
        elif is_keyword(arg):
            continue
        else:
            err("keyword-only formals must be alternating keywords"
                " and values, not %r" % arg)

    if arg is _keyword_starstar:
        starstar = next(iargs, undefined)
        if starstar is undefined:
            err("** keyword requires symbol binding")
        elif not (is_symbol(starstar) or is_lazygensym(starstar)):
            err("** keyword requires symbol binding, not %r" % star)
        arg = next(iargs, undefined)

    if arg is not undefined:
        err("leftover formals %r" % arg)

    return (positional, defaults, kwonly, star, starstar)


def helper_begin(
        fragment: ByteCodeFragment,
        body: pair,
        tc: bool = False) -> ByteCodeFragment:

    if body is nil:
        fragment.instruction(OpCode.OP_CONST, None)
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
            fragment.instruction(OpCode.OP_POP, 1)

    return fragment


__all__ = (
    "gather_formals",
    "gather_parameters",
    "helper_begin",
    "is_lazygensym",
)


# The end.
