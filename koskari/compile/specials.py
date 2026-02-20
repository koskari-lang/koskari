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
koskari.compile.specials

Special form handlers for the Koskari compiler.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from types import BuiltinMethodType
from ..lib import cons, FormalsSpec, FunctionSpec, keyword, nil, OpCode, pair, symbol
from ..types import get_position, is_nil, is_pair, is_symbol, set_position, unpack
from .compiler import ByteCodeFragment, Compiler, CompilerMode
from .exceptions import CompilerException
from .helpers import (
    const_only_expr,
    gather_bindings,
    gather_formals,
    gather_parameters,
    helper_begin,
    helper_binding,
    is_symbolish,
    list_star,
    pycons,
    requote,
    simple_parameters,
)


SPECIALS = {}


def special(form_symbol):
    """
    Decorator that registers a function as a special form handler.
    The decorated function gets a compile_impl attribute for dispatch.
    """

    def decorator(fn):
        SPECIALS[form_symbol] = fn
        fn.compile_impl = fn
        return fn

    return decorator


@special(symbol("compile/bytecode"))
def special_bytecode(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    fragment = compiler.fragment(called_by)

    for segm in body.unpack():
        op, opa = segm
        if opa is nil:
            opa = 0
        else:
            opa, rest = opa

        if isinstance(op, int):
            opc = OpCode(op)

        elif isinstance(op, (str, symbol, keyword)):
            op = str(op)
            if not op.startswith("KOS_OP_"):
                if op.startswith("OP_"):
                    op = "KOS_" + op
                else:
                    op = "KOS_OP_" + op

            opc = getattr(OpCode, op, None)

        else:
            opc = None

        if opc is None:
            raise TypeError("invalid Koskari OpCode: %r" % op)

        fragment.position_of(segm)
        fragment.instruction(opc, opa)

    return fragment


@special(symbol("compile/begin"))
def special_begin(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source
    fragment = compiler.fragment(called_by)
    return helper_begin(fragment, body, tc)



@special(symbol("compile/call"))
def special_call(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    fragment = compiler.fragment(called_by)

    # maybe check for (get-attr) here and transform to call-method
    fragment.position_of(body)
    obj, rest = body
    fragment.merge(compiler.full_compile(obj))

    params = gather_parameters(rest, source=rest)
    pos, kwds, values, vargs, vkwds = params

    if vargs or vkwds:
        # we'll be using CALL_STAR
        opa = 0
        if pos or vargs:
            opa = opa | 1
        if values or vkwds:
            opa = opa | 2

        if pos:
            for expr in pos:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.KOS_OP_BUILD_ARGS, len(pos))

        if vargs:
            fragment.merge(compiler.full_compile(vargs, False))
            if pos:
                fragment.instruction(OpCode.KOS_OP_COMBINE_ARGS, 2)

        if values:
            for expr in values:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.KOS_OP_BUILD_KWDS_NAMES,
                                 tuple(str(k) for k in kwds))

        if vkwds:
            fragment.merge(compiler.full_compile(vkwds, False))
            if values:
                fragment.instruction(OpCode.KOS_OP_COMBINE_KWDS, 2)

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.KOS_OP_TAIL_CALL_STAR, opa)
        else:
            fragment.instruction(OpCode.KOS_OP_CALL_STAR, opa)

    elif kwds:
        # we'll use CALL_NAMES
        opa = len(pos)

        for expr in pos:
            fragment.merge(compiler.full_compile(expr, False))

        for expr in values:
            fragment.merge(compiler.full_compile(expr, False))
        fragment.instruction(OpCode.KOS_OP_CONST,
                             tuple(str(k) for k in kwds))

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.KOS_OP_TAIL_CALL_NAMES, opa)
        else:
            fragment.instruction(OpCode.KOS_OP_CALL_NAMES, opa)

    else:
        # easy mode, we can just use CALL
        for expr in pos:
            fragment.merge(compiler.full_compile(expr, False))

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.KOS_OP_TAIL_CALL, len(pos))
        else:
            fragment.instruction(OpCode.KOS_OP_CALL, len(pos))

    return fragment


@special(symbol("compile/lambda"))
def special_lambda(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (lambda [args] body) or (lambda name [args] body).
    Named form binds name in the function scope for self-reference.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("lambda requires formals and body", source)

    first, rest_tail = rest
    if is_symbol(first):
        named = first
        if rest_tail is nil or not is_pair(rest_tail):
            raise CompilerException("named lambda requires formals and body",
                                   source)
        formals_src, body_src = rest_tail
    else:
        named = None
        formals_src, body_src = first, rest_tail

    if body_src is nil or not is_pair(body_src):
        body_src = pair(body_src, nil)

    proper_name = str(named) if named is not None else None
    fragment_name = proper_name if proper_name is not None else "<lambda>"

    pos, defaults, kwonly, star, starstar = gather_formals(
        formals_src, source=formals_src)
    kw = []
    pos = [str(k) for k in pos]
    star = str(star) if star else None
    starstar = str(starstar) if starstar else None

    frag = compiler.fragment(called_by)
    for k, expr in defaults:
        kw.append(str(k))
        frag.merge(compiler.full_compile(expr))

    if defaults:
        frag.instruction(OpCode.KOS_OP_CONST, tuple(kw))

    lamb = compiler.fragment(fragment_name)
    lamb.declare_vars(pos)
    lamb.declare_vars(kw)
    lamb.declare_vars([str(k) for k, _ in kwonly])
    if star:
        lamb.declare_var(star)
    if starstar:
        lamb.declare_var(starstar)
    if proper_name is not None:
        lamb.declare_var(proper_name)

    lamb = helper_begin(lamb, body_src, True)
    code = compiler.complete(lamb, CompilerMode.FUNCTION)
    fspec = FunctionSpec(proper_name, FormalsSpec(
        tuple(pos), (), tuple(kw), tuple(str(k) for k, _ in kwonly),
        star, starstar), code)
    frag.instruction(OpCode.KOS_OP_CONST, fspec)
    frag.instruction(OpCode.KOS_OP_BUILD_FUNCTION, len(kw))

    return frag


@special(symbol("compile/let"))
def special_let(
        compiler: Compiler,
        source: pair,
        tc: bool = False):
    """
    Compile (compile/let BINDINGS BODY...) or (compile/let NAME BINDINGS
    BODY...). Named let expands to an immediately-invoked named lambda:
    (let foo [[a 1] [b 2]] ...body) -> ((lambda foo [a b] ...body) 1 2).
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("let requires bindings and body", source)

    bindings, body = rest

    # TODO: adopt ceval-style named let: bind values as keyword defaults so
    # (let foo [[a 1] [b 2]] body) -> ((lambda foo [a: 1 b: 2] body)); allows
    # re-invocation with partial replacement.
    # Named let: (let NAME [[var val] ...] BODY...) -> ((lambda NAME [var ...] BODY) val ...)
    if is_symbolish(bindings):
        named = bindings
        bindings, body = body

        args, vals = gather_bindings(bindings, source)

        # Build (compile/lambda NAME [formal...] BODY...) using list_star so body_forms
        # is the tail; then build (compile/call LAMBDA val...) using variadic cons.

        src = (symbol('compile/call'),
               (symbol('compile/lambda'), named, cons(*args, nil), *body.unpack()),
               *vals)
        return pycons(src)

    # Unnamed let: (let [[var val] ...] BODY...) — push a child namespace, bind
    # names to compiled values, compile body in that scope, pop.
    args, vals = gather_bindings(bindings, source)
    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    with fragment.child_namespace(called_by, args, vals) as frag:
        helper_begin(frag, body, tc)

    return fragment


@special(symbol("compile/parameterize"))
def special_parameterize(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (compile/parameterize [[name val] ...] BODY...). Binds names as
    dynamic parameters for the duration of body; restores ambient values on
    exit. Uses PUSH_PARAM (values on stack, names tuple constant) and POP_PARAM.
    Consumers of parameters (e.g. functions that would use bind-param) read the
    current value via ParamRef(name).deref(); the body typically calls such
    consumers. See tests/compile/specials/test_parameterize.py.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("parameterize requires bindings and body",
                               source)
    bindings, body = rest
    args, vals = gather_bindings(bindings, source)
    fragment = compiler.fragment(called_by)
    fragment.position_of(source)

    for val in vals:
        fragment.merge(compiler.full_compile(val, False))

    fragment.instruction(OpCode.KOS_OP_PUSH_PARAM, tuple(map(str, args)))
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_PARAM, 0)
    return fragment


@special(symbol("compile/with"))
def special_with(compiler, source, tc=False):
    """
    Compile (compile/with [name? expr] BODY...). Single-shot context manager
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("with requires [name? expr] and body", source)

    spec, body = rest
    if spec is nil or not is_pair(spec):
        raise CompilerException("with requires [name? expr] list", source)

    name, expr = helper_binding(spec, allow_unnamed=True)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.merge(compiler.full_compile(expr, False))

    if name is not None:
        name = str(name)
        fragment.declare_var(name)

    fragment.instruction(OpCode.KOS_OP_PUSH_WITH, name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_WITH, 0)

    return fragment


@special(symbol("compile/dyn-with"))
def special_dyn_with(compiler, source, tc=False):
    """
    Compile (compile/dyn-with [name? expr] BODY...). Multi-shot context manager
    that participates in transfer plans (exit on unwind, enter on wind).
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("dyn-with requires [name? expr] and body", source)

    spec, body = rest
    if spec is nil or not is_pair(spec):
        raise CompilerException("dyn-with requires [name? expr] list", source)

    name, expr = helper_binding(spec, allow_unnamed=True)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.merge(compiler.full_compile(expr, False))

    if name is not None:
        name = str(name)
        fragment.declare_var(name)

    fragment.instruction(OpCode.KOS_OP_PUSH_DYN_WITH, name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_DYN_WITH, 0)

    return fragment


@special(symbol("compile/reset"))
def special_reset(compiler, source, tc=False):
    """
    Compile (compile/reset EXPR...). Establishes a prompt (tag 0), runs body,
    pops prompt. Enables delimited continuations via compile/shift.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("reset requires body", source)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.KOS_OP_PUSH_PROMPT, 0)
    helper_begin(fragment, rest, False)
    fragment.instruction(OpCode.KOS_OP_POP_PROMPT, 0)

    return fragment


@special(symbol("compile/shift"))
def special_shift(compiler, source, tc=False):
    """
    Compile (compile/shift K EXPR...). Captures continuation, binds to K, runs
    handler body. K must be a symbol; tag 0.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("shift requires continuation variable and body",
                               source)

    k, body = rest
    if not is_symbolish(k):
        raise CompilerException("shift continuation variable must be a symbol",
                               source)

    k_name = str(k)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.KOS_OP_PUSH_SHIFT, 0)
    fragment.declare_var(k_name)
    fragment.instruction(OpCode.KOS_OP_VAR_STORE, k_name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_SHIFT, 0)

    return fragment


@special(symbol("compile/setq"))
def special_setq(compiler, source, tc=False):
    called_by, (sym, body) = source
    expr, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("setq takes two arguments", source)

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(body)

    frag.request_var(sym)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/refq"))
def special_refq(compiler, source, tc=False):
    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("refq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_REF, sym)

    return frag


@special(symbol("compile/bindq"))
def special_bindq(compiler, source, tc=False):
    called_by, (sym, body) = source
    expr, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("bindq takes two arguments", source)

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(body)

    frag.declare_var(sym)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_BIND, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/delq"))
def special_delq(compiler, source, tc=False):
    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("delq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_CLEAR, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/attr"))
def special_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, rest) = body

    if rest is not nil:
        raise CompilerException("attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR, str(sym))

    return frag


@special(symbol("compile/del-attr"))
def special_del_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, rest) = body

    if rest is not nil:
        raise CompilerException("del-attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR_CLEAR, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/set-attr"))
def special_set_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, (val, rest)) = body

    if rest is not nil:
        raise CompilerException("set-attr takes three arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.merge(compiler.full_compile(val))
    frag.instruction(OpCode.KOS_OP_ATTR_STORE, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/ref-attr"))
def special_ref_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, rest) = body

    if rest is not nil:
        raise CompilerException("ref-attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR_REF, str(sym))

    return frag


@special(symbol("compile/item"))
def special_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, rest) = body

    if rest is not nil:
        raise CompilerException("item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM, 0)

    return frag


@special(symbol("compile/ref-item"))
def special_ref_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, rest) = body

    if rest is not nil:
        raise CompilerException("ref-item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_REF, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM_REF, 0)

    return frag


@special(symbol("compile/del-item"))
def special_del_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, rest) = body

    if rest is not nil:
        raise CompilerException("del-item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_CLEAR, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM_CLEAR, 0)

    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/set-item"))
def special_set_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, (val, rest)) = body

    if rest is not nil:
        raise CompilerException("set-item takes three arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_STORE, key_val)

    else:
        frag.merge(keyfrag)
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_ITEM_STORE, 0)

    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/quote"))
def special_quote(compiler, source, tc=False):
    called_by, body = source

    if body is nil or not is_pair(body):
        raise CompilerException("Too few arguments to quote", source)

    body, rest = body
    if rest is not nil:
        raise CompilerException("Too many arguments to quote", source)

    if is_pair(body) and not is_nil(body):
        body = requote(body)
        set_position(body, get_position(source))
        return body

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.instruction(OpCode.KOS_OP_CONST, body)
    return frag


@special(symbol("compile/car"))
def special_car(compiler, source, tc=False):
    """
    Compile (compile/car EXPR). Emit code that evaluates EXPR, then pushes
    the car of that pair. One argument.
    """

    called_by, body = source
    if body is nil or not is_pair(body):
        raise CompilerException("compile/car requires one argument", source)

    expr, rest = body
    if rest is not nil:
        raise CompilerException("compile/car takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_CAR, 0)

    return frag


@special(symbol("compile/cdr"))
def special_cdr(compiler, source, tc=False):
    """
    Compile (compile/cdr EXPR). Emit code that evaluates EXPR, then pushes
    the cdr of that pair. One argument.
    """

    called_by, body = source
    if body is nil or not is_pair(body):
        raise CompilerException("compile/cdr requires one argument", source)

    expr, rest = body
    if rest is not nil:
        raise CompilerException("compile/cdr takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_CDR, 0)

    return frag


_keyword_else = keyword("else")


@special(symbol("compile/cond"))
def special_cond(compiler, source, tc=False):
    """
    Compile (compile/cond (TEST BODY...) (TEST BODY...) ... (else: BODY...)).
    Conditionally executes body depending on a test expression. Multiple
    clauses; first truthful test's body is evaluated and returned. No match
    yields None. The else: keyword as test is always true.
    """

    called_by, cl = source

    fragment = compiler.fragment(called_by)

    gen_label = compiler.gen_label
    done = gen_label()
    label = gen_label()

    while cl:
        element, cl = cl

        fragment.position_of(element)
        fragment.instruction(OpCode.KOS_OP_LABEL, label)
        label = gen_label()

        test, body = element

        if test is _keyword_else:
            helper_begin(fragment, body, tc)
            break

        fragment.merge(compiler.full_compile(test))
        fragment.instruction(OpCode.KOS_OP_JUMP_FALSE, label)
        helper_begin(fragment, body, tc)
        fragment.instruction(OpCode.KOS_OP_JUMP, done)

    else:
        fragment.instruction(OpCode.KOS_OP_LABEL, label)
        fragment.instruction(OpCode.KOS_OP_CONST, None)

    fragment.instruction(OpCode.KOS_OP_LABEL, done)
    return fragment


def _expand_if_to_cond(rest, source):
    """
    Parse (compile/if COND THEN ELSE) or (compile/if COND then: THEN else: ELSE).
    Return a cond form suitable for full_compile.
    """

    pos, kw_dict = simple_parameters(rest, source)

    if kw_dict:
        cond_expr = pos[0] if pos else nil
        then_expr = kw_dict.get("then")
        else_expr = kw_dict.get("else")
        if then_expr is None or else_expr is None:
            raise CompilerException("if requires both then: and else: when using "
                                    "keywords", source)
    else:
        if len(pos) < 3:
            raise CompilerException("if requires condition, then, and else", source)
        cond_expr, then_expr, else_expr = pos[0], pos[1], pos[2]

    return pycons((symbol("compile/cond"),
                  (cond_expr, then_expr),
                  (_keyword_else, else_expr)))


def _expand_when_to_cond(rest, source):
    """
    Parse (compile/when COND BODY...). Return a cond form.
    """

    if rest is nil or not is_pair(rest):
        raise CompilerException("when requires condition and body", source)

    cond_expr, body = rest
    # body_tuple = tuple(unpack(body)) if is_pair(body) else (body,)
    return pycons((symbol("compile/cond"),
                   cons(cond_expr, body),
                   (_keyword_else, None)))


def _expand_unless_to_cond(rest, source):
    """
    Parse (compile/unless COND BODY...). Return a cond form.
    """

    if rest is nil or not is_pair(rest):
        raise CompilerException("unless requires condition and body", source)

    cond_expr, body = rest
    # body_tuple = tuple(unpack(body)) if is_pair(body) else (body,)
    return pycons((symbol("compile/cond"),
                   (cond_expr, None),
                   cons(_keyword_else, body)))


@special(symbol("compile/if"))
def special_if(compiler, source, tc=False):
    """
    Compile (compile/if COND THEN ELSE) or (compile/if COND then: THEN else: ELSE).
    Expands to cond and compiles.
    """

    called_by, rest = source
    expanded = _expand_if_to_cond(rest, source)
    return compiler.full_compile(expanded, tc)


@special(symbol("compile/when"))
def special_when(compiler, source, tc=False):
    """
    Compile (compile/when COND BODY...). Expands to cond; no else yields None.
    """

    called_by, rest = source
    expanded = _expand_when_to_cond(rest, source)
    return compiler.full_compile(expanded, tc)


@special(symbol("compile/unless"))
def special_unless(compiler, source, tc=False):
    """
    Compile (compile/unless COND BODY...). Expands to cond; negated test.
    """

    called_by, rest = source
    expanded = _expand_unless_to_cond(rest, source)
    return compiler.full_compile(expanded, tc)


@special(symbol("compile/while"))
def special_while(compiler, source, tc=False):
    """
    Compile (compile/while TEST BODY...). Loop with break and continue support.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("while requires test and body", source)

    test, body = rest
    fragment = compiler.fragment(called_by)
    loop_head = compiler.gen_label("while.head")
    loop_exit = compiler.gen_label("while.end")

    with fragment.child_loop(called_by) as child:
        child.instruction(OpCode.KOS_OP_LABEL, loop_head)
        child.merge(compiler.full_compile(test, False))
        child.instruction(OpCode.KOS_OP_JUMP_FALSE, loop_exit)
        helper_begin(child, body, False)
        child.instruction(OpCode.KOS_OP_JUMP, loop_head)
        child.instruction(OpCode.KOS_OP_LABEL, loop_exit)
        child.instruction(OpCode.KOS_OP_CLEAR_LOOP, 0)

    return fragment


def _special_break_continue(compiler, source, op):
    """
    Shared logic for break and continue. Emit op with count 0 or 1.
    """

    called_by, rest = source

    if rest is nil:
        frag = compiler.fragment(called_by)
        frag.position_of(source)
        frag.instruction(op, 0)
        return frag

    val, tail = rest
    if tail is not nil:
        raise CompilerException("%s takes at most one argument" % called_by,
                               source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.merge(compiler.full_compile(val, False))
    frag.instruction(op, 1)
    return frag


@special(symbol("compile/break"))
def special_break(compiler, source, tc=False):
    """
    Compile (compile/break) or (compile/break VAL). Exits innermost loop.
    """

    return _special_break_continue(compiler, source, OpCode.KOS_OP_BREAK)


@special(symbol("compile/continue"))
def special_continue(compiler, source, tc=False):
    """
    Compile (compile/continue) or (compile/continue VAL). Restarts loop.
    """

    return _special_break_continue(compiler, source, OpCode.KOS_OP_CONTINUE)


@special(symbol("compile/cons"))
def special_cons(compiler, source, tc=False):
    """
    Variadic: (compile/cons EXPR1 EXPR2 ... EXPR_TAIL). Compile each
    expression in order (stack bottom = first element, top = tail), then
    emit CONS with arg = number of expressions. At least one argument
    (the tail).
    """

    called_by, body = source
    if body is nil or not is_pair(body):
        raise CompilerException("compile/cons requires at least one argument",
                               source)

    exprs = list(body.unpack())
    frag = compiler.fragment(called_by)
    frag.position_of(source)

    for expr in exprs:
        frag.merge(compiler.full_compile(expr))

    frag.instruction(OpCode.KOS_OP_CONS, len(exprs))

    return frag


# The end.
