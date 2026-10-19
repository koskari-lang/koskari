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
koskari.compile.helpers

Shared helpers for the special-form compilers: gathering formals,
bindings and declarations from a form's captures, rerouting and
splatting call forms, partitioning collection runs, emitting item
access and item places, requoting forms, discovering docstrings,
compiling begin bodies, and the unpacking left-hand-side helpers

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from typing import List

from ..exceptions import ParametersError, SpecialError
from ..lib import (
    DatumFrame, OpCode, bracket_nil, bracket_pair, car, cdr, cons, demand,
    keyword, nil, pair, symbol)
from ..matcher import (
    BINDING, BINDING_MATCHERS, BINDINGS, FORMALS, LIST, LITERAL_KW, MATCH,
    REST, SYMBOL, USING, USING_MATCHERS, USING_UNPACK, VALUE, first_match,
    frame_formal_name, make_matcher)
from ..types import (
    is_const, is_keyword, is_nil, is_pair, is_proper, is_symbol,
    strip_mark, unpack)
from .compiler import ByteCodeFragment


__all__ = (
    "NO_DEFAULT",
    "BINDING",
    "FORMALS",
    "BINDINGS",
    "USING",
    "USING_UNPACK",
    "const_only_expr",
    "convert_bindings",
    "discover_docstring",
    "emit_item_access",
    "emit_item_place",
    "feeder_expr",
    "gather_bindings",
    "gather_formals",
    "gather_parameters",
    "helper_begin",
    "helper_binding",
    "helper_elsebody",
    "helper_using",
    "parse_decl",
    "pycons",
    "requote",
    "splat_call_form",
    "strip_leading_docstring",
    "unpack_lhs_bind_symbols",
    "using_unpack_helper",
)


_SYM_CALL = symbol("call")


def splat_call_form(called_by, body):
    """
    Lower a variadic special to its call form when a keyword argument is
    present.

    Variadic builders such as ``#list``, ``#tuple``, and ``#str`` cannot
    emit a ``*:`` / ``**:`` splat directly. When BODY contains any keyword
    argument, rewrite the source into ``(call CALLED_BY . BODY)`` so
    the ``call`` special merges the splatted arguments; the operator symbol
    resolves to its runtime builtin as the callee.

    This is the non-reducing counterpart to the fold-then-reroute performed
    by the reducing operators: the check happens before any argument is
    consumed, so it must not be used where constant folding of leading
    arguments is desired.

    :param called_by: the operator symbol heading the source form
    :param body: the argument list pair (may be nil)
    :return: a ``(call ...)`` source pair when a keyword is present,
        otherwise None
    """

    for fm in unpack(body):
        if is_keyword(fm):
            return call_form_reroute(called_by, body)

    return None


_SYM_BEGIN = symbol('begin')
_BEGIN_HEADS = (_SYM_BEGIN,)


_keyword_star = keyword("*")
_keyword_starstar = keyword("**")

_PYCONS_DIRECT = (
    str, bytes, bool, int, float, complex,
    type(None), type(...),
    symbol, keyword, type(nil), type(bracket_nil), pair,
)
_PYCONS_RECONS = (tuple, list)


def helper_binding(source):
    """
    Parse a single binding form ``(SYM EXPR)`` or ``(EXPR as: SYM)``
    """

    msg = ("a binding must be in the form of (SYM EXPR) or (EXPR as: SYM),"
           f" not {source}")

    if not is_proper(source):
        raise SpecialError(msg, source)

    _index, m = first_match(cons(source, nil), BINDING_MATCHERS)
    if m is None:
        raise SpecialError(msg, source)

    name = m.get("name")
    value = m.get("expr")

    return (
        name.datum if name else None,
        value.datum)


def helper_using(source):
    """
    Parse a single using form ``(EXPR)``, ``(SYM EXPR)``, or ``(EXPR as: SYM)``
    """

    msg = ("a using must be in the form of (EXPR), (SYM EXPR),"
           f" or (EXPR as: SYM), not {source}")

    if not is_proper(source):
        raise SpecialError(msg, source)

    _index, m = first_match(cons(source, nil), USING_MATCHERS)
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


def _parse_decl_tail(tail, source):
    """
    Parse REST after a declaration name: only keyword attributes, no value
    """

    if is_nil(tail):
        return {}
    head = car(tail)
    if not is_keyword(head):
        raise SpecialError(
            "declaration cannot include a value",
            source)

    ann = {}
    cur = tail
    while not is_nil(cur):
        kw = strip_mark(car(cur))
        cur = cdr(cur)
        if not is_keyword(kw):
            raise SpecialError(
                f"expected keyword in declaration attribute list, not {kw!r}",
                source)
        if is_nil(cur):
            raise SpecialError(
                f"missing value for declaration attribute {kw!r}",
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
                f"unknown declaration attribute {kw!r}",
                source)
    return ann


# the declaration forms parse_decl tells apart, each matcher built
# once and run over a one-cell spine of the form
_decl_with_value = demand(make_matcher(LIST(SYMBOL("name"), VALUE("expr"))))
_decl_as_name = demand(make_matcher(
    LIST(VALUE("type_expr"), LITERAL_KW("as"), SYMBOL("name"))))
_decl_formal_head = demand(make_matcher(
    LIST(MATCH(SYMBOL("name"), REST("tail")))))


def parse_decl(form, source=None):
    """
    Parse a binding-shaped declaration with no value
    """

    if is_symbol(form):
        return (form, None)
    if not is_pair(form):
        raise SpecialError(
            f"declaration must be symbol or proper list, not {form!r}",
            source)
    if not is_proper(form):
        raise SpecialError(
            f"declaration list must be proper, not {form!r}",
            source)

    elems = tuple(unpack(form))
    if len(elems) == 1 and is_symbol(elems[0]):
        return (elems[0], None)

    subject = cons(form, nil)
    if _decl_with_value(subject) is not None:
        raise SpecialError(
            "declaration cannot include a value",
            source)

    m_as = _decl_as_name(subject)
    if m_as is not None:
        return (m_as["name"].datum, m_as["type_expr"].datum)

    m = _decl_formal_head(subject)
    if m is None:
        raise SpecialError(f"invalid declaration form {form!r}", source)

    name = m["name"].datum
    tail = m["tail"].datum
    ann = _parse_decl_tail(tail, source)
    type_expr = ann.get("type")
    return (name, type_expr)


def gather_parameters(args, source=None):
    """
    Parse call-site argument list into interleaved args_seq and kwds_seq
    """

    undefined = object()

    if is_symbol(args):
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
            f"parameters must be symbol or pair, not {args!r}", source)

    args_seq = []
    kwds_seq = []
    pos_index = 0
    in_kwds_phase = False

    iargs = iter(unpack(args))
    arg = next(iargs, undefined)

    while arg is not undefined:
        label = arg
        if is_keyword(label):
            if label is _keyword_star:
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

            elif label is _keyword_starstar:
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
                        f"missing value for keyword parameter {label}",
                        source)
                kwds_seq.append((str(label), value))

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
        if op in (OpCode.NOOP, OpCode.POSITION):
            continue
        if op != OpCode.CONST or found:
            return (False, None)
        found = True
        value = opa

    return (found, value)


def var_only_expr(fragment):
    """
    Check whether a ByteCodeFragment consists only of a single VAR load,
    answering (True, atom) with the loaded name's atom for one and
    (False, None) otherwise
    """

    found = False
    name = None

    for op, opa in fragment.instructions:
        if op in (OpCode.NOOP, OpCode.POSITION):
            continue
        if op != OpCode.VAR or found:
            return (False, None)
        found = True
        name = opa

    return (found, name)


def feeder_expr(fragment):
    """
    Classify a compiled fragment as a feeder the compress pipeline
    folds: ('const', value) for a single CONST, ('var', atom) for a
    single VAR naming atom, (None, None) for anything else
    """

    found, value = const_only_expr(fragment)
    if found:
        return ('const', value)
    is_var, name = var_only_expr(fragment)
    if is_var:
        return ('var', name)
    return (None, None)


def emit_item_access(compiler, called_by, obj, key, stack_op, named_op):
    """
    Emit an item access with no value, for compile/item and the CLEAR,
    TAKE, and REF specials: the object, the key, and stack_op, which
    the compress pipeline folds when the key and object are feeders.
    When the object compiles to a bare name load and the key is not a
    feeder, the key compiles to the stack and named_op names the
    object, since no fold reaches an object load behind a computed
    key. A symbol that compiles to anything else, nil among them,
    stays on the stack

    :param compiler: the Compiler
    :param called_by: the special's called-by match
    :param obj: the object match
    :param key: the key match
    :param stack_op: the verb's row over a stack object and key
    :param named_op: the verb's row over a named object and stack key
    :return: the fragment
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    objfrag = compiler.full_compile(obj.datum)
    keyfrag = compiler.full_compile(key.datum)
    obj_kind, obj_name = feeder_expr(objfrag)
    key_kind, _ = feeder_expr(keyfrag)

    if obj_kind == 'var' and key_kind is None:
        # the object's load becomes the named operand; its request
        # carries over without its instruction
        frag.merge(keyfrag)
        for sym in objfrag.requests:
            frag.request_var(sym)
        frag.instruction(named_op, obj_name)
    else:
        frag.merge(objfrag)
        frag.merge(keyfrag)
        frag.instruction(stack_op, 0)
    return frag


ITEM_FEEDER_OP = {'var': OpCode.VAR, 'const': OpCode.CONST}


def emit_item_place(compiler, frag, obj, keyfrag, key, value_feeders):
    """
    Emit the object and key of a store or in-place row, answering the
    feeder opcodes the row names for them, None for a stack operand.
    Plain when the key and every value are feeders: the object and the
    key compile to the stack and the compress pipeline folds the row's
    trailing feeders. Otherwise a non-feeder operand would sit between
    a named operand and the row, so the object is named when it
    compiles to a bare name load (nil is a symbol that does not) and
    the key when a feeder, and only the rest compiles to the stack

    :param compiler: the Compiler
    :param frag: the fragment being built
    :param obj: the object match
    :param keyfrag: the compiled key
    :param key: the key's feeder pair from feeder_expr
    :param value_feeders: whether every value is a feeder
    :return: (obj_op, key_op, fields) for item_opcode and item_arg
    """

    key_kind, key_val = key
    objfrag = compiler.full_compile(obj.datum)
    obj_kind, obj_name = feeder_expr(objfrag)
    if obj_kind != 'var':
        obj_name = None
    if (key_kind and value_feeders) or not (obj_name or key_kind):
        frag.merge(objfrag)
        frag.merge(keyfrag)
        return None, None, []

    fields = []
    obj_op = key_op = None
    # a named operand's request carries over without its instruction
    if obj_name is None:
        frag.merge(objfrag)
    else:
        for sym in objfrag.requests:
            frag.request_var(sym)
        obj_op = OpCode.VAR
        fields.append(obj_name)
    if key_kind is None:
        frag.merge(keyfrag)
    else:
        for sym in keyfrag.requests:
            frag.request_var(sym)
        key_op = ITEM_FEEDER_OP[key_kind]
        fields.append(key_val)
    return obj_op, key_op, fields


def call_form_reroute(called_by, body):
    """
    Rewrite a variadic builder into its ``(call CALLED_BY . BODY)``
    form so the ``call`` special assembles keyword / double-splat arguments.
    """

    return cons(_SYM_CALL, called_by, body)


def partition_collection_runs(compiler, args_seq):
    """
    Group the positional / splat arguments of a collection literal into
    contiguous runs. ``args_seq`` is the positional half of a
    :func:`gather_parameters` result: each entry is ``("*", expr)`` for a
    ``*:`` splat or ``(index, expr)`` for a plain element. Yields chunks in
    source order:

      ("const", (v0, v1, ...))       -- a run of poolable constant values,
                                        already merged into one tuple
      ("expr", [frag0, frag1, ...])  -- a run of dynamic-expression
                                        fragments, each leaving one value
      ("splat", frag)                -- a *: segment whose fragment leaves one
                                        iterable on the stack

    An element is treated as constant only when it compiles to a single
    CONST whose value is valid constant-pool material (:func:`koskari.types.is_const`);
    this excludes bare symbols (which compile to variable loads) while
    folding literals and quoted constants. Adjacent constants merge into one
    tuple; adjacent plain expressions gather into one list; a splat always
    stands alone.
    """

    chunks = []
    const_run = []
    expr_run = []

    def flush_const():
        if const_run:
            chunks.append(("const", tuple(const_run)))
            const_run.clear()

    def flush_expr():
        if expr_run:
            chunks.append(("expr", list(expr_run)))
            expr_run.clear()

    for kind, element in args_seq:
        if kind == "*":
            flush_const()
            flush_expr()
            chunks.append(("splat", compiler.full_compile(element)))
            continue

        frag = compiler.full_compile(element)
        found, value = const_only_expr(frag)
        if found and is_const(value):
            flush_expr()
            const_run.append(value)
        else:
            flush_const()
            expr_run.append(frag)

    flush_const()
    flush_expr()
    return chunks


def requote(
        source,
        cons_sym=None,
        quote_sym=None):
    """
    Rewrite a list form so each element is wrapped in ``(quote_sym ELEMENT)``.

    The reconstruction preserves the source's delimiter style: a bracket-pair
    spine rebuilds via the ``bracket-cons`` special (terminating with
    ``bracket-nil``), a plain pair spine via the ``cons`` special
    (the ``CONS`` opcode). Nested elements are re-quoted, so each sublist's
    own style is detected recursively.
    """

    if cons_sym is None:
        if isinstance(source, bracket_pair):
            cons_sym = symbol("bracket-cons")
        else:
            cons_sym = symbol("cons")
    if quote_sym is None:
        quote_sym = symbol("quote")

    col = []
    while True:
        element, rest = source
        col.append(cons(quote_sym, element, nil))

        if is_nil(rest):
            # preserve the terminator flavor (nil vs bracket-nil)
            col.append(rest)
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

    while not is_nil(body):
        head, tail = body
        if isinstance(head, str):
            return head, tail

        if is_nil(head) or not is_pair(head):
            return None, None

        ihead, itail = head
        if ihead in _BEGIN_HEADS:
            if is_nil(itail):
                body = tail
                continue
            doc, new_itail = _strip_leading_docstring_body(itail)
            if doc is None:
                return None, None
            if is_nil(new_itail):
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
    Strip a trailing ``[else: BODY...]`` clause from a body list

    :return: ``(body, elsebody)`` where elsebody is the list of forms
        after the else marker, to be compiled as an implicit begin;
        None when the clause is present but empty, nil when absent
    """

    collected = []
    current = source

    while not is_nil(current):
        head, tail = current
        if is_nil(tail) and is_pair(head):
            e, ebody = head
            if e is _keyword_else:
                body_list = cons(*collected, nil) if collected else nil
                if is_nil(ebody):
                    return body_list, None
                else:
                    return body_list, ebody
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

    if is_symbol(lhs):
        yield lhs
        return

    if not is_proper(lhs):
        fail("unpack pattern must be a symbol or a proper list")

    items = list(unpack(lhs))
    if not items:
        fail("empty unpack pattern")

    star_idx = [
        i for i, elt in enumerate(items)
        if elt is _keyword_star]

    if len(star_idx) > 1:
        fail("at most one * is allowed in an unpack pattern")

    if not star_idx:
        for elt in items:
            if flatten and is_pair(elt):
                yield from unpack_lhs_bind_symbols(elt, source, flatten)
            elif not is_symbol(elt):
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
    if not is_symbol(star_name):
        fail("name after * in unpack pattern must be a symbol")

    for elt in prefix:
        if flatten and is_pair(elt):
            yield from unpack_lhs_bind_symbols(elt, source, flatten)
        elif not is_symbol(elt):
            fail("unpack pattern list elements must be symbols")
        else:
            yield elt

    yield star_name

    for elt in suffix:
        if flatten and is_pair(elt):
            yield from unpack_lhs_bind_symbols(elt, source, flatten)
        elif not is_symbol(elt):
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

    if is_symbol(lhs):
        maybe_declare(lhs)
        frag.instruction(OpCode.VAR_STORE, lhs)
        return frag

    if not is_proper(lhs):
        fail("unpack pattern must be a symbol or a proper list")

    items = list(unpack(lhs))
    if not items:
        fail("empty unpack pattern")

    if len(items) == 2 \
       and is_symbol(items[0]) \
       and is_symbol(items[1]):
        maybe_declare(items[0])
        maybe_declare(items[1])
        frag.instruction(
            OpCode.UNPACK_VAR, (items[0], items[1], 0, 0))
        return frag

    star_idx = [
        i for i, elt in enumerate(items)
        if elt is _keyword_star]

    if len(star_idx) > 1:
        fail("at most one * is allowed in an unpack pattern")

    if not star_idx:
        n = len(items)
        frag.instruction(OpCode.UNPACK, n)
        for elt in items:
            if is_pair(elt):
                frag.merge(using_unpack_helper(compiler, elt, source, declare))
            elif is_symbol(elt):
                maybe_declare(elt)
                frag.instruction(OpCode.VAR_STORE, elt)
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
    if not is_symbol(star_name):
        fail("name after * in unpack pattern must be a symbol")

    if not prefix and not suffix:
        maybe_declare(star_name)
        frag.instruction(OpCode.VAR_STORE, star_name)
        return frag

    left = len(prefix)
    right = len(suffix)
    frag.instruction(OpCode.UNPACK_STAR, (left, right, 0, 0))
    for elt in prefix:
        if is_pair(elt):
            frag.merge(using_unpack_helper(compiler, elt, source, declare))
        elif is_symbol(elt):
            maybe_declare(elt)
            frag.instruction(OpCode.VAR_STORE, elt)
        else:
            fail("unpack pattern list elements must be symbols or pairs")

    maybe_declare(star_name)
    frag.instruction(OpCode.VAR_STORE, star_name)

    for elt in suffix:
        if is_pair(elt):
            frag.merge(using_unpack_helper(compiler, elt, source, declare))
        elif is_symbol(elt):
            maybe_declare(elt)
            frag.instruction(OpCode.VAR_STORE, elt)
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

    return strip_leading_docstring(body)[0]


def helper_begin(
        fragment: ByteCodeFragment,
        body: pair,
        *,
        position=None) -> ByteCodeFragment:
    """
    Compile a body list into a fragment

    :param fragment: the fragment to compile into
    :param body: the body list to compile
    :param position: enclosing source position threaded to diagnostics
        about positionless atoms in the body
    :return: the compiled fragment
    """

    if is_nil(body):
        fragment.instruction(OpCode.CONST, None)
        return fragment

    compiler = fragment.compiler

    while True:
        fragment.position_of(body)

        expr, body = body
        # layout bodies position the statement form but not the spine
        # cell holding it, so the cell alone is not enough here
        fragment.position_of(expr)
        if is_nil(body):
            fragment.merge(compiler.full_compile(expr, position=position))
            break
        else:
            fragment.merge(compiler.full_compile(expr, position=position))
            fragment.instruction(OpCode.POP, 1)

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


# the default slot of a formal with no default: positional and
# keyword-only alike, so a nil default, the source form (), is a value
NO_DEFAULT = object()


def _attrs_frames_to_ann(attr_frames: List[dict], source):
    """
    Fold formal attribute REPEAT frames into (ann dict, default or
    NO_DEFAULT, by_ref bool)
    """

    ann = {}
    default = NO_DEFAULT
    by_ref = False
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
        elif kw == keyword('byref'):
            by_ref = bool(val)
        else:
            raise SpecialError(
                f"unknown formal attribute {kw!r}",
                source)
    return ann, default, by_ref


def _strip_byref_kw(kw):
    """
    Strip a leading & from a keyword name; return (keyword, by_ref).

    A keyword like ``&acc`` is the terse keyword by-ref form: ``&acc:`` reads
    as the keyword named ``&acc``
    """

    s = str(kw)
    if s[:1] == '&':
        return keyword(s[1:]), True
    return kw, False


def _strip_byref_sym(sym):
    """
    Strip a leading & from a symbol name; return (symbol, by_ref).

    Covers the reader-disabled by-ref form where ``&x`` survives as a literal
    symbol rather than expanding to ``(ref x)``
    """

    s = str(sym)
    if s[:1] == '&':
        return symbol(s[1:]), True
    return sym, False


def _gather_one_positional_frame(fr: dict, source):
    """
    Map one positional REPEAT frame to (name, ann_bundle, default_expr,
    by_ref)
    """

    name, by_ref = frame_formal_name(fr)
    typ = None if (v := fr.get('type')) is None else v.datum

    if "default" in fr:
        default_expr = fr['default'].datum
    else:
        default_expr = NO_DEFAULT

    kw_cell = fr.get('kw')
    if kw_cell is not None:
        _reject_bare_annotation(kw_cell.datum, default_expr, None, source)

    annotations = {}
    if typ is not None:
        annotations['type'] = typ

    attrs_cell = fr.get('attrs')
    if attrs_cell:
        ann2, default2, byref2 = _attrs_frames_to_ann(attrs_cell, source)
        for k2, v2 in ann2.items():
            if k2 in annotations and annotations[k2] != v2:
                raise SpecialError(
                    f"conflicting formal annotations for {name!r}",
                    source)
            annotations[k2] = v2
        if default2 is not NO_DEFAULT:
            if default_expr is not NO_DEFAULT:
                raise SpecialError(
                    f"duplicate default for formal {name!r}",
                    source)
            default_expr = default2
        if byref2:
            by_ref = True

    if name is None:
        raise SpecialError(
            f"positional frame missing name: {fr!r}",
            source)
    if not is_symbol(name):
        raise SpecialError(
            "positional formals must be symbols or bracket forms, not"
            f" {name!r}",
            source)

    return name, annotations, default_expr, by_ref


def _reject_bare_annotation(kw, typ, prev_name, source):
    """
    Raise when a defaults or kw-only frame carries the empty keyword.

    A bare :: inside a formals list reads as the empty keyword, so the
    matcher takes it for a defaulted parameter named "". The error spells
    the parenthesized annotation form using the preceding formal and the
    value that followed the ::

    :param kw: the frame keyword
    :param typ: the value that followed the keyword
    :param prev_name: the formal that preceded the keyword, or None
    :param source: spine for error context
    :raises SpecialError: when kw is the empty keyword
    """

    if str(kw) != "":
        return
    name = "name" if prev_name is None else str(prev_name)
    shown = repr(typ) if isinstance(typ, str) else str(typ)
    raise SpecialError(
        "bare :: in formals list; annotate a parameter as"
        f" [({name} :: {shown})]",
        source)


def gather_formals(formals_group, source=None):
    """
    Parse matcher FORMALS GROUP dict into seven values for compile specials

    Tuple order matches historical compile formals unpacking, with by_refs
    appended:
    (positional, defaults, kwonly, star, starstar, annotations, by_refs)

    :param formals_group: dict from captures[formals_slot] after match
    :param source: optional spine for error context; defaults to
        ``formals_group.source`` when omitted
    :return: (positional, defaults, kwonly, star, starstar, annotations,
        by_refs)
    :raises SpecialError: on invalid structure
    """

    if source is None and isinstance(formals_group, DatumFrame):
        source = formals_group.source

    annotations = {}
    positional = []
    defaults = []
    by_refs = []
    prev_name = None

    for fr in formals_group.get('positionals', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid positional frame {fr!r}",
                source)
        name, ann_bundle, default, by_ref = \
            _gather_one_positional_frame(fr, source)
        _merge_formal_annotations(annotations, name, ann_bundle)
        if by_ref:
            by_refs.append(str(name))
        if default is not NO_DEFAULT:
            defaults.append((keyword(str(name)), default))
        else:
            positional.append(name)
        prev_name = name

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
        _reject_bare_annotation(kw, dflt, prev_name, source)
        kw, by_ref = _strip_byref_kw(kw)
        if by_ref:
            by_refs.append(str(kw))
        defaults.append((kw, dflt))
        prev_name = kw

    kwonly = []
    for fr in formals_group.get('kwonly', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid kwonly frame {fr!r}",
                source)
        name = None if (v := fr.get('name')) is None else v.datum
        if name is not None and is_symbol(name):
            name, by_ref = _strip_byref_sym(name)
            if by_ref:
                by_refs.append(str(name))
            kwonly.append((keyword(str(name)), NO_DEFAULT))
            prev_name = name
            continue
        kw = None if (v := fr.get('kw')) is None else v.datum
        dflt = None if (v := fr.get('default')) is None else v.datum
        if kw is not None:
            _reject_bare_annotation(kw, dflt, prev_name, source)
            kw, by_ref = _strip_byref_kw(kw)
            if by_ref:
                by_refs.append(str(kw))
            kwonly.append((kw, dflt))
            prev_name = kw
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

    if star is not None and not is_nil(star):
        if not is_symbol(star):
            raise SpecialError(
                f"* keyword requires symbol binding, not {star!r}",
                source)

    return (positional, defaults, kwonly, star, starstar, annotations,
            tuple(by_refs))


# The end.
