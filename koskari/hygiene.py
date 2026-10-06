# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
koskari.hygiene

Template expansion and expansion finalize for syntax transformers.

Hygiene is provenance: a symbol a template introduced becomes a gensym
carrying the expansion that introduced it, and the compiler declares
and resolves it under the spelling the gensym derives. A keyword is a
label and carries no marks. Material substituted from the use site
arrives in Datum boxes from the matcher and is never marked. Finalize
peels the Datum boxes for compilation and leaves the gensyms for the
compiler

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.exceptions import CompilerError
from koskari.lib import (
    Datum, FunctionSpec, bracket_cons, bracket_nil, bracket_pair, cons,
    gensym, nilp, pair, symbol)
from koskari.types import (
    build_proper, get_position, is_nil, is_pair, is_symbol, unpack)


__all__ = (
    "Deferred",
    "HygieneFunctionSpec",
    "Resolver",
    "binding_atom",
    "candidates",
    "declared_spellings",
    "equal_by_base",
    "expand_template_with_hygiene",
    "extern_atoms",
    "finalize_expansion",
    "function_constant",
    "mark_atom",
    "mark_expansion",
    "peel_datums",
)


# --- Marking


def mark_atom(atom, mark):
    """
    The atom scoped by ``mark``: a symbol becomes a gensym over it, a
    gensym gains ``mark`` as its newest, and anything else, keywords
    included, is returned unchanged

    :param atom: a source atom
    :param mark: the expansion mark, an int
    """

    if isinstance(atom, gensym):
        return gensym(atom.base, marks=atom.marks + (mark,))
    if is_symbol(atom):
        return gensym(atom, marks=mark)
    return atom


# --- Template expansion


_symbol_datumq = symbol("datumq")


def _is_datumq_directive(form):
    """
    Whether form is a template pair headed by the raw datumq symbol
    """

    return is_pair(form) and not is_nil(form) and form.car is _symbol_datumq


def _datumq_directive(template):
    """
    The Datum a (datumq X) template pair emits: X raw, unmarked, with the
    position of the spine cell that holds it

    :param template: a template pair headed by the raw datumq symbol
    :raises CompilerError: when the directive does not hold one form
    """

    rest = template.cdr
    if not is_pair(rest) or is_nil(rest) or not is_nil(rest.cdr):
        count = len(list(unpack(rest))) if is_pair(rest) else 0
        raise CompilerError(
            "datumq in a template takes exactly one form, got"
            f" {count}: {template}",
            template)
    return Datum(rest.car, get_position(rest))


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    # a directive's form is emitted raw, so its names bind nothing
    if _is_datumq_directive(expr):
        return

    if is_pair(expr):
        for elem in unpack(expr):
            if is_symbol(elem):
                out.add(elem)
            elif is_pair(elem):
                _collect_symbols(elem, out)


def _binding_value_for_expand(val):
    """
    Peel a single outer ``Datum`` wrapper when present so list, pair, or tuple
    shape is visible to template expansion. The bindings dict still holds
    matcher captures unchanged elsewhere
    """

    return val.datum if isinstance(val, Datum) else val


def _repeat_seq(val):
    """
    Return the ellipsis-dimension sequence a binding still carries, or None.

    Repeat captures are promoted to bare Python ``list``/``tuple`` columns (one
    entry per iteration of the enclosing ``...``); a rank-N variable nests N
    such sequences deep, with leaves that are ``Datum`` wrappers or plain
    captured atoms. A Koskari pair, a ``Datum``, or an atom is a *leaf*, not a
    dimension, so only a bare ``list``/``tuple`` (after peeling one ``Datum``)
    counts as an unconsumed ellipsis level
    """

    peeled = _binding_value_for_expand(val)
    if isinstance(peeled, (list, tuple)):
        return peeled
    return None


def _controlling_seqs(template, bindings):
    """
    Map each symbol in ``template`` that still carries an ellipsis dimension to
    its current-level sequence. These variables drive one ``...`` iteration and
    are peeled one dimension per level via :func:`_slice_bindings`
    """

    syms = set()
    _collect_symbols(template, syms)
    out = {}
    for s in syms:
        if s in bindings:
            seq = _repeat_seq(bindings[s])
            if seq is not None:
                out[s] = seq
    return out


def _ellipsis_count(controlling, template):
    """
    Common iteration length across all controlling sequences at one ``...``
    level, or 0 when none are present. Divergent lengths are an error
    """

    counts = {len(seq) for seq in controlling.values()}
    if not counts:
        return 0
    if len(counts) != 1:
        raise CompilerError(
            'incompatible ellipsis repetition lengths', template)
    return next(iter(counts))


def _slice_bindings(bindings, controlling, index):
    """
    Narrow ``bindings`` for one iteration of a ``...`` level: each controlling
    variable is replaced by its ``index``-th element (peeling one ellipsis
    dimension), while every other binding passes through unchanged
    """

    sub = dict(bindings)
    for sym, seq in controlling.items():
        sub[sym] = seq[index]
    return sub


def expand_template_with_hygiene(template, bindings, mark=None):
    """
    Substitute pattern bindings into a template and mark the symbols
    the template itself introduces

    Ellipsis repetition is depth-aware: a variable captured under N pattern
    ellipses carries N nested repeat sequences, and each template ``...`` peels
    exactly one dimension (see :func:`_slice_bindings`). A variable referenced
    under fewer ellipses than its rank still carries an unconsumed sequence at
    use site and raises, rather than splicing a raw column into the output

    :param template: template form (pairs, symbols, or opaque atoms)
    :param bindings: dict from pattern variable symbol to capture value
        (often ``Datum``-wrapped); a single outer ``Datum`` wrapper is peeled
        only when expanding list or ellipsis structure, not when returning
        an unexpanded binding reference unchanged
    :param mark: the expansion mark to box template atoms with, or None
        to substitute without marking
    """

    if is_symbol(template):
        if template in bindings:
            val = bindings[template]
            if _repeat_seq(val) is not None:
                raise CompilerError(
                    'ellipsis variable used at insufficient ellipsis depth',
                    template)
            return val

        if mark is None:
            return template
        return mark_atom(template, mark)

    if not is_pair(template):
        return template

    # (datumq X) emits X raw; a transcription consumes the directive,
    # so a nested one reaches the inner template as a raw form
    if _is_datumq_directive(template):
        return _datumq_directive(template)

    # the spine's elements, and a dotted template's terminator apart
    # from them so it stays the terminator
    items = []
    cell = template
    while is_pair(cell) and not is_nil(cell):
        items.append(cell.car)
        cell = cell.cdr
    tail = expand_template_with_hygiene(cell, bindings, mark) \
        if not is_nil(cell) else None

    out = []
    i = 0
    while i < len(items):
        elem = items[i]

        rep = (i + 1 < len(items) and items[i + 1] is Ellipsis)
        if not rep:
            out.append(
                expand_template_with_hygiene(elem, bindings, mark))
            i += 1
            continue

        # a lone pattern variable bound to a non-repeat value (a pair spine
        # from a REST capture) splices like quasiquote ,@ rather than iterating
        if (is_symbol(elem)
                and elem in bindings
                and _repeat_seq(bindings[elem]) is None):
            spliced = _binding_value_for_expand(bindings[elem])
            if spliced is not None:
                out.extend(unpack(spliced))
            i += 2
            continue

        controlling = _controlling_seqs(elem, bindings)
        count = _ellipsis_count(controlling, elem)
        for rep_i in range(count):
            sub = _slice_bindings(bindings, controlling, rep_i)
            out.append(
                expand_template_with_hygiene(elem, sub, mark))
        i += 2

    # preserve the template's delimiter style: a bracket-pair template
    # rebuilds as a bracket list (terminated by bracket-nil), so a [ ] in a
    # syntax-rules template survives expansion instead of flattening to ( )
    if isinstance(template, bracket_pair):
        if tail is not None:
            return bracket_cons(*out, tail)
        return bracket_cons(*out, bracket_nil) if out else bracket_nil

    if tail is not None:
        return cons(*out, tail)
    return build_proper(*out)


# --- Expansion finalize


def mark_expansion(form, mark, seen=None):
    """
    A copy of a procedural expansion with every symbol outside a
    ``Datum`` scoped by ``mark``. A ``Datum`` is substituted material
    and is carried over as it is; a pair inside one is never entered.
    Pair cells are rebuilt with their positions and delimiter style. A
    cell that encloses itself is a cycle, which source cannot hold; a
    sub-form placed at more than one position is copied at each

    :param form: the handler's output
    :param mark: the expansion mark
    :param seen: ids of the cells enclosing this form on the walk
    :return: the marked copy
    :raises CompilerError: when the output is cyclic
    """

    if isinstance(form, Datum):
        return form

    if not is_pair(form) or is_nil(form):
        return mark_atom(form, mark)

    if seen is None:
        seen = set()

    items = []
    tail = form
    while is_pair(tail) and not is_nil(tail):
        if id(tail) in seen:
            raise CompilerError(
                "macro expansion is cyclic; a form cannot contain itself",
                form)
        seen.add(id(tail))
        items.append(tail)
        tail = tail.cdr
        if isinstance(tail, Datum):
            break

    if isinstance(tail, Datum):
        end = tail
    else:
        end = mark_atom(tail, mark)

    out = end
    for cell in reversed(items):
        car = mark_expansion(cell.car, mark, seen)
        if isinstance(cell, bracket_pair):
            out = bracket_cons(car, out)
        else:
            out = cons(car, out)
        pos = get_position(cell)
        if pos:
            out.set_position(pos)

    # this spine is left; its cells may appear again under a sibling
    seen.difference_update(id(cell) for cell in items)
    return out


def peel_datums(form):
    """
    Peel ``Datum`` boxes off a finished expansion in place, so the
    compiler sees the substituted source. A ``Datum`` car is unboxed and
    its position moved onto the cell; the material inside is the user's
    code and is not entered. ``Datum`` layers along a cdr chain are
    unboxed the same way

    :param form: expansion output (pairs, atoms, ``Datum``, gensyms)
    :rtype: same root object as ``form`` after in-place updates where
        applicable
    """

    seen = set()

    def peel_car_in_place(nx):
        c = nx.car
        first_pos = None
        if isinstance(c, Datum):
            first_pos = c.position
            c = c.datum
        if first_pos is not None:
            nx.position = first_pos
        nx.car = c
        return nx.car

    def peel_cdr_datums_in_place(nx):
        tail = nx.cdr
        while isinstance(tail, Datum):
            tail = tail.datum
            nx.cdr = tail
            tail = nx.cdr
        return tail

    def walk_spine(nx):
        while not is_nil(nx):
            if not is_pair(nx):
                return
            i = id(nx)
            if i in seen:
                nx = nx.cdr
                continue
            seen.add(i)

            car_was_datum = isinstance(nx.car, Datum)
            c = peel_car_in_place(nx)
            if is_pair(c) and not car_was_datum:
                walk_spine(c)

            tail = peel_cdr_datums_in_place(nx)
            if not is_pair(tail):
                return
            nx = tail

    if isinstance(form, Datum):
        form = form.datum
    if is_pair(form):
        walk_spine(form)
    return form


def finalize_expansion(form, mark=None):
    """
    Finish one expansion for the compiler: mark a procedural arm's
    output when ``mark`` is given, then peel the ``Datum`` boxes

    :param form: the expansion output
    :param mark: the expansion mark for a procedural arm, or None when
        transcription marked the output already
    :return: the finished form
    """

    if mark is not None:
        form = mark_expansion(form, mark)
    return peel_datums(form)


def equal_by_base(left, right):
    """
    Whether two forms are equal once every gensym is read as its base
    symbol, without copying either. Pair spines are walked together,
    two symbols compare by base identity, and any other pair of values
    compares by equality

    :param left: a form
    :param right: a form
    :return: True when the forms are equal up to marks
    """

    while left is not right:
        if isinstance(left, pair) and isinstance(right, pair):
            if nilp(left) or nilp(right):
                return nilp(left) and nilp(right)
            if not equal_by_base(left.car, right.car):
                return False
            left, right = left.cdr, right.cdr
            continue

        if isinstance(left, symbol) and isinstance(right, symbol):
            return left.base is right.base

        return left == right

    return True


def extern_atoms(pattern, names):
    """
    The atoms a pattern form writes for the given base spellings, as
    a mapping from spelling to atom. A pattern lowers each name it
    reads to its base spelling; this recovers the atom the name was
    written as, so its load resolves as the template wrote it

    :param pattern: the pattern source form
    :param names: the base spellings to recover
    :raises CompilerError: when one spelling is written as two
        different atoms in the pattern
    """

    wanted = set(names)
    found = {}
    stack = [pattern]
    while stack:
        form = stack.pop()
        if is_pair(form) and not is_nil(form):
            stack.append(form.cdr)
            stack.append(form.car)
        elif isinstance(form, symbol):
            spelling = str(form.base)
            if spelling not in wanted:
                continue
            prior = found.setdefault(spelling, form)
            if prior is not form:
                raise CompilerError(
                    f"pattern names two bindings spelled {spelling}",
                    pattern, position=get_position(pattern))
    return found


def binding_atom(compiler, atom):
    """
    The atom a binder declares: in a class body, an attribute of the
    class, a label named by its bare spelling; elsewhere the atom as
    written, so a template's binder stays its own

    :param compiler: the compiler, whose innermost open scope decides
    :param atom: the binder's symbol or gensym
    """

    scopes = compiler.lexical_stack
    if scopes and scopes[-1].collector_scope:
        return atom.base
    return atom


# --- Resolution

def candidates(atom):
    """
    The symbols a reference may resolve to, longest first: a gensym,
    then the gensym under its marks with the newest removed, down to
    its base. A bare symbol has itself alone

    :param atom: a symbol or gensym
    :rtype: list of symbol
    """

    base = atom.base
    if base is atom:
        return [atom]
    marks = atom.marks
    out = [atom]
    for n in range(len(marks) - 1, 0, -1):
        out.append(gensym(base, marks=marks[:n]))
    out.append(base)
    return out


def declared_spellings(fragment):
    """
    The spellings a fragment declares or pulls from the module
    """

    return {str(n) for n in fragment.declares} | {
        str(n) for n in fragment._request_global}


class Resolver:
    """
    The declarations visible to one completing fragment: its own, those
    of the fragments enclosing it, and the module's bound names

    A marked spelling bound in the module was defined by an earlier
    turn, as a macro-generating macro's expansion leaves behind

    :param fragment: the fragment being completed
    :param ancestors: the fragments enclosing it, outermost first
    :param module_names: the module's bound names
    """

    __slots__ = ("declared", "module_names")


    def __init__(self, fragment, ancestors, module_names):
        self.declared = [declared_spellings(fragment)]
        self.declared.extend(
            declared_spellings(f) for f in reversed(ancestors))
        self.module_names = module_names


    def choose(self, atom, scopes=()):
        """
        The first candidate for atom whose spelling is declared in an
        inline scope, the fragment, an ancestor, or the module; the
        base when none is

        :param atom: a symbol or gensym
        :param scopes: spelling sets of the inline scopes open around
            the reference, innermost first
        """

        cands = candidates(atom)
        for cand in cands[:-1]:
            spelling = str(cand)
            if any(spelling in s for s in scopes) \
                    or any(spelling in s for s in self.declared) \
                    or spelling in self.module_names:
                return cand
        return cands[-1]


    def resolve(self, fragment):
        """
        Resolve every marked name the fragment references to the binding
        it names, in place: its request buckets and the variable operands
        of its instruction stream

        A marked reference tries its key, then the key with the newest
        mark removed, down to the bare name, against the declarations
        visible from its position: the inline scopes open around it in
        the instruction stream, the fragment's own, and those of the
        fragments enclosing it, which are final by the time it completes

        The inline scopes compiled into the fragment have their write
        buckets resolved the same way, with the scopes open around each
        write, so validation reads the names the stream binds

        :param fragment: the fragment being completed
        """

        self.resolve_bucket(fragment.requests)
        self.resolve_bucket(fragment.requests_for_write,
                            positions=fragment.write_positions)
        self.resolve_bucket(fragment._request_global,
                            positions=fragment.declare_positions)

        # a name resolved to the fragment's own declaration or global is
        # no request, as the intake of a written name already ensures
        declares = set(fragment.declares)
        globals_ = set(fragment._request_global)
        fragment.requests[:] = [
            n for n in fragment.requests if n not in declares]
        fragment.requests_for_write[:] = [
            n for n in fragment.requests_for_write
            if n not in declares and n not in globals_]

        for scope, ancestors in fragment.resolution.inline_scopes(fragment):
            inner = ancestors[ancestors.index(fragment) + 1:]
            scopes = [declared_spellings(scope)]
            scopes.extend(declared_spellings(f) for f in reversed(inner))
            for bucket in (scope.requests_for_write,
                           scope.propagated_writes):
                self.resolve_bucket(bucket, scopes,
                                    positions=scope.write_positions)

        fragment.rewrite_vars(self.choose)


    def resolve_bucket(self, bucket, scopes=(), positions=None):
        """
        Replace each name in bucket with its choice, in place, keeping
        the first of any names that choose the same binding. A source
        position recorded under a name moves with it to the chosen
        name, so a diagnostic raised after the names have settled
        still finds the line the request came from

        :param bucket: a list of atoms
        :param scopes: spelling sets of the inline scopes open around
            the names, innermost first
        :param positions: the fragment's position record for this
            bucket, keyed by atom, or None
        """

        seen = set()
        out = []
        for sym in bucket:
            chosen = self.choose(sym, scopes)
            if positions is not None and chosen is not sym:
                pos = positions.get(sym)
                if pos is not None:
                    positions.setdefault(chosen, pos)
            if chosen not in seen:
                seen.add(chosen)
                out.append(chosen)
        bucket[:] = out


# --- Deferred completion

class Deferred:
    """
    A constant awaiting completion, standing in for its final value in
    the enclosing fragment's CONST row until that fragment completes

    :param fragment: the fragment to complete
    :param ancestors: the fragments lexically open around it when it
        closed, outermost first
    """

    __slots__ = ("fragment", "ancestors")


    def __init__(self, fragment, ancestors):
        self.fragment = fragment
        self.ancestors = ancestors


    def chain(self, completer, ancestors):
        """
        The ancestry the fragment completes under: the completer's own
        chain, then the completer when the snapshot lacks it, then the
        snapshot, outermost first throughout

        A function fragment was on the lexical stack while its body
        compiled; the root never is, and encloses the rest

        :param completer: the fragment whose completion reaches this
        :param ancestors: the fragments enclosing the completer
        """

        chain = ancestors
        if completer not in self.ancestors:
            chain = chain + (completer,)
        return chain + self.ancestors


    def complete(self, compiler, completer, ancestors):
        """
        The final value for the CONST row

        :param compiler: the compiler completing the completer
        :param completer: the fragment whose completion reaches this
        :param ancestors: the fragments enclosing the completer
        """

        raise NotImplementedError


class HygieneFunctionSpec(Deferred):
    """
    A function body whose marked names resolve against declarations
    that are final only when the enclosing fragment completes: a
    sibling defined later in the same body, as the functions of a
    labels form are, must be visible to a closure defined before it

    :param fragment: the function body fragment
    :param ancestors: the fragments lexically open around the body
        when it closed, outermost first
    :param name: the function name, or None
    :param formals_spec: the FormalsSpec
    """

    __slots__ = ("name", "formals_spec")


    def __init__(self, fragment, ancestors, name, formals_spec):
        super().__init__(fragment, ancestors)
        self.name = name
        self.formals_spec = formals_spec


    def complete(self, compiler, completer, ancestors):
        code = compiler.complete_function(
            self.fragment, self.chain(completer, ancestors))
        return FunctionSpec(self.name, self.formals_spec, code)


def needs_outside_choice(body):
    """
    Whether a function body's names depend on declarations outside it:
    it references a marked name it does not declare or pull global
    itself, or it holds a deferred function, whose requests reach it
    only when that completes

    :param body: the function body fragment
    """

    own = set(body._request_global)
    for bucket in (body.requests, body.requests_for_write):
        for name in bucket:
            if name.base is not name and name not in own:
                return True

    return body.holds_deferred


def function_constant(compiler, parent, body, name, formals_spec):
    """
    The CONST value for a function body: its FunctionSpec, completed
    now, when no name in the body depends on declarations outside it;
    otherwise a HygieneFunctionSpec completing with the parent, which
    is marked in turn so its own completion waits for the declarations
    around it

    :param compiler: the compiler
    :param parent: the fragment the CONST row is emitted into
    :param body: the function body fragment
    :param name: the function name, or None
    :param formals_spec: the FormalsSpec
    """

    ancestors = tuple(compiler.lexical_stack)
    if not (body.marked and needs_outside_choice(body)):
        code = compiler.complete_function(body, ancestors)
        return FunctionSpec(name, formals_spec, code)
    parent.marked = True
    parent.holds_deferred = True
    return HygieneFunctionSpec(body, ancestors, name, formals_spec)


# The end.
