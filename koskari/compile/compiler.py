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
koskari.compile.compiler

The koskari compiler. Converts source forms into bytecode fragments.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import warnings
import weakref
from abc import ABC, abstractmethod
from contextlib import contextmanager
from enum import IntEnum, auto

from ..exceptions import CompilerError, UncaughtCompilerError
from ..lib import (
    Arena, Code, Datum, FormalsSpec, FunctionSpec, Mark, Module,
    ModuleNamespace, Namespace, NamespaceSpec, OpCode, OpStyle, bracket_nil,
    bracket_pair, cons, keyword, kos_repr, nil, symbol)
from ..types import (
    fill_position, get_position, is_keyword, is_mark, is_nil, is_pair,
    is_proper, is_symbol, parameterize, strip_mark, strip_marks)
from .pragma import (
    BracketCalls, KeywordLiterals, KoskariPragma, PackModule, ResolutionMode,
    StrayColon)
from .processor import ByteCodeProcessor


class Compiled(ABC):

    @abstractmethod
    def compile_impl(self, compiler, source_obj):
        """
        Compile a source object into a bytecode fragment
        """

        ...


CONST_TYPES = (
    str, bytes,
    bool, int, float, complex,
    slice,
    type(None), type(...),
    symbol, keyword, type(nil), type(bracket_nil),
    FunctionSpec,
    NamespaceSpec,
)

CONST_CONTAINER_TYPES = (tuple, frozenset)


def _is_const_container(value):
    if not isinstance(value, CONST_CONTAINER_TYPES):
        return False
    return all(_is_const(x) for x in value)


def _is_const(value):
    return isinstance(value, CONST_TYPES) or _is_const_container(value)


def _label_generator(formatstr="label_%04x"):
    counter = 0

    def gen_label(text=None):
        nonlocal counter
        counter += 1
        if text is None:
            return formatstr % counter
        return "%s_%x" % (text, counter)

    return gen_label


_symbol_None = symbol("None")
_symbol_True = symbol("True")
_symbol_False = symbol("False")
_symbol_attr = symbol("attr")
_symbol_Ellipsis = symbol("Ellipsis")
_symbol_ellipsis = symbol("...")
_symbol_nil = symbol("nil")

_symbol_call = symbol("call")
_symbol_cons = symbol("cons")

_symbol_colon = symbol(":")

# the parameter name (active-pragma) reads; the compiler binds it for
# each compilation turn
ACTIVE_PRAGMA_PARAM = "active-compiler-pragma"


def _arena_member_size(code):
    """
    The bytes a code object's unified block occupies inside an arena:
    the unrounded block total, aligned up to the arena's reservation
    unit, since each member starts on a unit boundary

    :param code: a Code object
    """

    size = code.payload_bytes + code.padding_bytes
    unit = Arena.UNIT
    return ((size + unit - 1) // unit) * unit


class ByteCodeFragment:
    """
    Holds a fragment of bytecode during compilation: instructions plus
    closure requests and name declarations for the namespace
    """

    def __init__(self, compiler=None, name=None, filename=None,
                 resolution=None):
        """
        :param compiler: the compiler/builder used to complete this fragment
        :param name: optional fragment name (e.g. for a block or loop)
        :param filename: optional source filename
        :param resolution: resolution instance; default KoskariPragma when omitted
        """

        self.name = name
        self.filename = filename
        self.compiler = compiler
        self.resolution = resolution or KoskariPragma()
        self.instructions = []
        self.requests = []
        self.requests_for_write = []
        self._request_global = []
        self.declares = []
        self.annotation_bindings = []
        self.doc = None

        # write requests copied up from inner Codes at completion; they
        # are validated where they originate, so the validator skips
        # them here (see KoskariPragma.cpool_merge)
        self.propagated_writes = []

        # set by Compiler.resolution_scope for a function body: the
        # scope a Code is built from, as opposed to an inline let or
        # class body compiled into its enclosing Code
        self.function_scope = False

        # set by collector_namespace for a class body: its bindings
        # become the class's attributes, whose names are labels
        self.collector_scope = False


    def as_namespace_spec(self):
        """
        Build a NamespaceSpec from this fragment's name and declarations.
        """

        return self.resolution.build_namespace_spec(self)


    def request_var(self, var_name):
        """
        Record a closure request for var_name unless it is already declared

        :param var_name: name to request from enclosing scope
        """

        if is_mark(var_name):
            self.resolution.note_mark(var_name)
        self.resolution.on_request_var(self, str(var_name))


    def request_for_write(self, var_name, position=None):
        """
        Record that var_name must be writable (setq, delq, inplace).

        :param var_name: name that must be writable
        :param position: optional (line, col) for error reporting
        """

        if is_mark(var_name):
            self.resolution.note_mark(var_name)
        self.resolution.on_write(self, str(var_name), position=position)


    def request_global(self, var_name, position=None):
        """
        Record explicit need from module (define-global).

        :param var_name: name to pull from module
        :param position: optional (line, col) for error reporting
        """

        if is_mark(var_name):
            self.resolution.note_mark(var_name)
        self.resolution.on_explicit_global(self, str(var_name), position=position)


    def declare_var(self, var_name, position=None, type_expr=None):
        """
        Record a name declared in this fragment's namespace.

        :param var_name: name to declare
        :param position: optional (line, col) for error reporting
        :param type_expr: optional source form for a type annotation (compile
            metadata only; not evaluated here)
        """

        self.resolution.on_declare(self, str(var_name), position=position)
        if type_expr is not None:
            self.annotation_bindings.append((str(var_name), type_expr))


    def declare_vars(self, var_names):
        """
        Record multiple names declared in this fragment's namespace

        :param var_names: iterable of names to declare
        """

        for var_name in var_names:
            self.declare_var(var_name)


    def position_of(self, source):
        """
        Emit a position instruction from the source's position if set

        :param source: a pair or object with get_position
        """

        pos = get_position(source)
        if pos:
            return self.position(pos)


    def position(self, linecol, extra=None):
        """
        Emit a position instruction

        :param linecol: (line, column) or position tuple
        :param extra: optional extra position data (e.g. source text)
        """

        if linecol:
            line, col = linecol[:2]
            self.instruction(OpCode.POSITION, (line, col, extra))


    def instruction(self, op, opa):
        """
        Append one bytecode instruction

        :param op: OpCode for the instruction
        :param opa: argument for the instruction
        """

        self.instructions.append((op, opa))


    def merge(self, other):
        """
        Merge another fragment into this one: adopt its declarations,
        requests, requests_for_write, request_global, and
        annotation_bindings, then append its instructions.

        Used when combining forms in the same scope (e.g. begin body);
        request_global must propagate so that (define-global x) followed
        by (setq x 1) in the same block works.
        """

        for name in other.declares:
            self.declare_var(name)
        for name in other.requests:
            self.request_var(name)
        for name in other.requests_for_write:
            self.request_for_write(name)
        for name in other._request_global:
            if name not in self.declares:
                self.request_global(name)
        self.annotation_bindings.extend(other.annotation_bindings)
        self.instructions.extend(other.instructions)


    @contextmanager
    def child_namespace(self, ns_name, names, values):
        """
        Context manager that compiles a child scope with bound names.
        On entry, compiles all value expressions and yields a fragment for
        the body. On exit, emits value expressions (if any), then PUSH_NS with
        nspec and names in the EXTRA row, body instructions, POP_NS.

        :param ns_name: name for the child namespace (e.g. source form)
        :param names: sequence of bound names (strings or symbols)
        :param values: sequence of expressions (one per name)
        """

        compiler = self.compiler
        ns_name = str(ns_name)
        child = compiler.fragment(ns_name)
        with compiler.resolution_scope(child):
            names = tuple(map(str, names))
            if len(names) != len(values):
                raise CompilerError(
                    "mismatch between names and values",
                    (names, values))

            if names:
                for name in names:
                    child.declare_var(name)
                valfrag = compiler.fragment(ns_name)
                for val in values:
                    valfrag.merge(compiler.full_compile(val))

            yield child
            nspec = child.as_namespace_spec()
            child.resolution.register_scope_fragment(
                child, tuple(f for f in compiler.lexical_stack if f is not child))

            # A name the scope closes over is a closure need of the
            # enclosing scope as well: the enclosing function captures
            # the cell in its own slot at creation, and the scope's
            # PUSH_NS resolves it from the frame. A namespace's parent
            # link skips call frames, so the frame is the only place
            # the scope can reach it.
            for name in nspec.closure_syms:
                if name not in self.declares:
                    self.request_var(name)

            if names:
                self.merge(valfrag)

            self.instruction(
                OpCode.PUSH_NS,
                (nspec, tuple(names) if names else None, 0, 0))
            self.instructions.extend(child.instructions)
            self.instruction(OpCode.POP_NS, 0)


    @contextmanager
    def collector_namespace(self, ns_name):
        """
        Context manager for a collector namespace (CollectorNamespace).
        Like child_namespace but uses PUSH_COLLECT/POP_COLLECT. POP_COLLECT
        pushes the collected namespace onto the stack.

        :param ns_name: name for the child namespace
        :param names: sequence of bound names (strings or symbols)
        :param values: sequence of expressions (one per name)
        """

        compiler = self.compiler
        ns_name = str(ns_name)
        child = compiler.fragment(ns_name)
        child.collector_scope = True
        with compiler.resolution_scope(child):
            yield child
            nspec = child.as_namespace_spec()
            child.resolution.register_scope_fragment(
                child, tuple(f for f in compiler.lexical_stack if f is not child))
            for name in nspec.closure_syms:
                if name not in self.declares:
                    self.request_var(name)

            self.instruction(OpCode.PUSH_COLLECT, nspec)
            self.instructions.extend(child.instructions)
            self.instruction(OpCode.POP_COLLECT, 0)


    def as_function_spec(self, formals_spec=((), ())):
        if self.compiler is None:
            raise TypeError("ByteCodeFragment has no compiler")
        code = self.compiler.complete(self, CompilerMode.FUNCTION)
        return FunctionSpec(self.name, FormalsSpec(*formals_spec), code)


    def as_function(self, parent, formals_spec=((), ()), defaults=None):
        if defaults is None:
            defaults = {}
        spec = self.as_function_spec(formals_spec)
        return spec(defaults, parent)


    def as_expression(self, parent):
        if self.compiler is None:
            raise TypeError("ByteCodeFragment has no compiler")
        code = self.compiler.complete(
            self, mode=CompilerMode.EXPRESSION, is_root=True)
        return code.expression(parent)


class CompilerMode(IntEnum):
    FUNCTION = auto()
    EXPRESSION = auto()


class DeferredFunction:
    """
    A function body awaiting completion, standing in for its
    FunctionSpec in the enclosing fragment's CONST row until the
    enclosing fragment completes.

    A nested function resolves the marked names it references against
    the declarations visible around it, and those are final only when
    the fragment enclosing it completes: a sibling defined later in the
    same body, as the functions of a labels form are, must be visible
    to a closure defined before it. So a function body is completed
    when its enclosing fragment is, and the FunctionSpec takes its place
    then

    :param fragment: the function body fragment
    :param name: the function name, or None
    :param formals_spec: the FormalsSpec
    :param ancestors: the fragments lexically open around the body
        when it closed, outermost first
    """

    __slots__ = ("fragment", "name", "formals_spec", "ancestors")


    def __init__(self, fragment, name, formals_spec, ancestors):
        self.fragment = fragment
        self.name = name
        self.formals_spec = formals_spec
        self.ancestors = ancestors


def mark_candidates(mark):
    """
    The keys a marked reference may resolve to, longest first: its own
    key, then the key with the newest mark removed, down to the bare
    name

    :param mark: a Mark
    :rtype: list of str
    """

    marks = mark.marks
    out = []
    for n in range(len(marks), 0, -1):
        out.append(str(Mark(mark.datum, marks[:n])))
    out.append(str(mark.datum))
    return out


def _choose_key(candidates, scopes, module_names):
    """
    The first candidate declared in a visible scope, or, for a marked
    spelling, already bound in the module; the bare name when none is

    A marked spelling bound in the module was defined by an earlier
    turn, as a macro-generating macro's expansion leaves behind
    """

    for cand in candidates:
        for scope in scopes:
            if cand in scope:
                return cand
        if cand is not candidates[-1] and cand in module_names:
            return cand
    return candidates[-1]


def resolve_marks(fragment, ancestors):
    """
    Resolve every marked name the fragment references to the binding
    it names, in place, at the fragment's completion when its own
    declarations are final.

    A marked reference tries its key, then the key with the newest mark
    removed, down to the bare name, against the declarations visible
    from its position: the inline scopes open around it in the
    instruction stream, the fragment's own, and the fragments that
    enclose the fragment, whose declarations are final by the time it
    completes (see DeferredFunction). An inline scope's closure names
    resolve from outside that scope

    :param fragment: the fragment being completed
    :param ancestors: fragments lexically enclosing it, outermost first
    """

    reso = fragment.resolution
    marks = reso.marks
    compiler = fragment.compiler
    module_names = vars(compiler.module) if compiler is not None else {}

    def declared(frag):
        return set(frag.declares) | set(frag._request_global)

    base = [declared(fragment)]
    base.extend(declared(f) for f in reversed(ancestors))

    def choose(name, scopes):
        mark = marks.get(name)
        if mark is None:
            return name
        return _choose_key(mark_candidates(mark), scopes, module_names)

    def rename_bucket(bucket, scopes):
        seen = []
        for name in bucket:
            chosen = choose(name, scopes)
            if chosen not in seen:
                seen.append(chosen)
        bucket[:] = seen

    rename_bucket(fragment.requests, base)
    rename_bucket(fragment.requests_for_write, base)
    rename_bucket(fragment._request_global, base)

    def rebuild_nspec(nspec, scopes):
        closure = []
        for name in nspec.closure_syms:
            chosen = choose(name, scopes)
            if chosen not in closure:
                closure.append(chosen)
        if tuple(closure) == tuple(nspec.closure_syms):
            return nspec
        return NamespaceSpec(
            nspec.name, nspec.local_syms, tuple(closure), nspec.global_syms)

    def scope_names(nspec):
        # a global an inline scope declares is bound in the module and
        # visible within the scope under that spelling, as its locals are
        return set(nspec.local_syms) | set(nspec.global_syms)

    # the stream is the compiler's own emission: PUSH_NS and
    # PUSH_COLLECT carry their NamespaceSpec as the operand, and the
    # fused rows the processor mints later are not here yet
    scopes = list(base)
    out = []
    for op, arg in fragment.instructions:
        if op == OpCode.PUSH_NS:
            nspec = rebuild_nspec(arg[0], scopes)
            arg = (nspec,) + tuple(arg[1:])
            scopes.insert(0, scope_names(nspec))

        elif op == OpCode.PUSH_COLLECT:
            arg = rebuild_nspec(arg, scopes)
            scopes.insert(0, scope_names(arg))

        elif op in (OpCode.POP_NS, OpCode.POP_COLLECT):
            scopes.pop(0)

        elif op.style == OpStyle.VAR:
            arg = choose(arg, scopes)

        elif op.extra and OpStyle.VAR in op.extra:
            arg = tuple(
                choose(v, scopes) if fk == OpStyle.VAR else v
                for v, fk in zip(arg, op.extra))

        out.append((op, arg))

    fragment.instructions = out


_COMPILER_LITERALS = {
    _symbol_None: None,
    _symbol_True: True,
    _symbol_False: False,
    _symbol_Ellipsis: ...,
    _symbol_ellipsis: ...,
    _symbol_nil: nil,
}


class Compiler:

    def __init__(
            self, name, module, filename=None,
            tco_enabled=True,
            resolution_mode=ResolutionMode.KOSKARI,
            bracket_calls=BracketCalls.IGNORE,
            stray_colon=StrayColon.ALLOW,
            keyword_literals=KeywordLiterals.ALLOW):

        self.name = name

        # the ModulePragma this compiler answers to, attached by
        # reload_pragma; None for a compiler built without one
        self.pragma = None

        # settings, possibly from pragma
        self.convert_fast_enabled = True
        self.tco_enabled = tco_enabled
        self.resolution_mode = resolution_mode
        self._resolution_instance = None
        self.bracket_calls = bracket_calls
        self.stray_colon = stray_colon
        self.keyword_literals = keyword_literals
        self.protect_code = False
        self.pack_module = PackModule(True, 0, 0)
        self.cache_expression = False
        self.cache_function = True
        self.rewrite_limit = 32

        # position of the innermost form compiled so far, for attributing
        # diagnostics about source objects that carry no position
        self._last_position = None

        # weakrefs to every Code minted through complete(), in
        # definition order; the live remainder at seal() time is the
        # module's durable code set
        self._minted = []

        # nested scope depth (function, let, class bodies); 0 = module
        # top level. Maintained by resolution_scope; consulted by
        # specials restricted to module level (e.g. import-all-from)
        self.scope_depth = 0

        # the fragments whose bodies are open right now, outermost
        # first: the lexical chain enclosing whatever is being compiled.
        # resolution_scope pushes the fragment of each nested scope; a
        # nested Code completed while the chain is open records it as
        # its ancestry for write validation at the root
        self.lexical_stack = []

        if module is None:
            module = Module(ModuleNamespace(name, {}))
        elif isinstance(module, dict):
            module = Module(ModuleNamespace(name, module))
        elif isinstance(module, Namespace):
            module = Module(module)

        self.module = module

        self.gen_label = _label_generator()

        ns = vars(module)

        r = ns.ref("__name__")
        if r.empty:
            r.store(name)
        elif name is None:
            name = r.deref()
        self.name = name

        r = ns.ref("__file__")
        if r.empty:
            r.store(filename)
        elif filename is None:
            filename = r.deref()
        self.filename = filename

        self._macro_overlay = []


    @classmethod
    def from_pragma(
            cls, name, module, filename=None,
            pragma=None):

        """
        Create a compiler from a pragma, or a default pragma and its settings.
        """

        if pragma is None:
            from ..pragma import ModulePragma
            pragma = ModulePragma()

        inst = cls(name, module, filename=filename)
        inst.reload_pragma(pragma)
        pragma.watch(inst)
        return inst


    def reload_pragma(self, pragma):
        """
        Reload the compiler from a pragma.
        """

        self.pragma = pragma

        self.convert_fast_enabled = pragma.get("compiler/convert-fast")
        self.tco_enabled = pragma.get("compiler/tco-enable")

        self.resolution_mode = pragma.get("compiler/resolution")
        self._resolution_instance = None

        self._bracket_calls = pragma.get("compiler/call-brackets")
        self._stray_colon = pragma.get("compiler/stray-colon")
        self._keyword_literals = pragma.get("compiler/keyword-literals")

        self.protect_code = pragma.get("compiler/protect-code", False)
        self.pack_module = pragma.get(
            "compiler/pack-module", PackModule(True, 0, 0))

        self.cache_expression = pragma.get("compiler/cache-expression", False)
        self.cache_function = pragma.get("compiler/cache-function", True)

        self.rewrite_limit = pragma.get("compiler/rewrite-limit", 32)


    @property
    def bracket_calls(self):
        return self._bracket_calls


    @bracket_calls.setter
    def bracket_calls(self, value):
        if not isinstance(value, BracketCalls):
            value = BracketCalls(value)
        self._bracket_calls = value


    @property
    def stray_colon(self):
        return self._stray_colon


    @stray_colon.setter
    def stray_colon(self, value):
        if not isinstance(value, StrayColon):
            value = StrayColon(value)
        self._stray_colon = value


    @property
    def keyword_literals(self):
        return self._keyword_literals


    @keyword_literals.setter
    def keyword_literals(self, value):
        if not isinstance(value, KeywordLiterals):
            value = KeywordLiterals(value)
        self._keyword_literals = value


    @property
    def resolution_mode(self):
        return self._resolution_mode


    @resolution_mode.setter
    def resolution_mode(self, value):
        if not isinstance(value, ResolutionMode):
            value = ResolutionMode(value)
        self._resolution_mode = value
        self._resolution_pragma_class = value.resolution_class


    @contextmanager
    def macro_overlay(self, name, syntax):
        if self._macro_overlay:
            dfns = dict(self._macro_overlay[-1])
            dfns[str(name)] = syntax
        else:
            dfns = {str(name): syntax}
        self._macro_overlay.append(dfns)
        try:
            yield
        finally:
            self._macro_overlay.pop()


    @contextmanager
    def macro_overlays(self, **dfns):
        """
        Context manager that overlays the macro namespace with the given
        definitions.
        """

        if self._macro_overlay:
            dfns = dict(self._macro_overlay[-1], **dfns)
        self._macro_overlay.append(dfns)
        try:
            yield
        finally:
            self._macro_overlay.pop()


    @contextmanager
    def resolution_scope(self, fragment=None, function=False):
        """
        Bracket the compilation of a nested scope's body

        :param fragment: the fragment the body compiles into; while the
            body is open it sits on the lexical stack as an enclosing
            scope of anything completed inside it
        :param function: the fragment is a function body, the kind of
            scope a Code is built from
        """

        reso = self._resolution_instance
        if reso is not None:
            reso.enter_scope()
        self.scope_depth += 1
        if fragment is not None:
            if function:
                fragment.function_scope = True
            self.lexical_stack.append(fragment)
        try:
            yield
        finally:
            if fragment is not None:
                self.lexical_stack.pop()
            self.scope_depth -= 1
            if reso is not None:
                reso.exit_scope()


    def _find_compiled(self, name):
        name = str(name)

        if self._macro_overlay:
            dfns = self._macro_overlay[-1]
            v = dfns.get(name)
            if v and isinstance(v, Compiled):
                return v

        # compiled forms live in the builtins below the module's own
        # values, so this is a resolution, not a read of the module
        ns = vars(self.module)
        v = ns.resolve(name, None)
        if v and isinstance(v, Compiled):
            return v
        return None


    def find_compiled(self, name):
        """
        Resolve compile/<name> in the module namespace

        :param name: symbol or string for the compile-time object (e.g. if, begin)
        :return: Compiled instance, or None
        """

        # a marked head names a transformer the same expansion may
        # have defined under the marked spelling, else the bare one
        if is_mark(name):
            for cand in mark_candidates(name):
                found = self._find_compiled_use(cand)
                if found is not None:
                    return found
            return None

        return self._find_compiled_use(str(name))


    def _find_compiled_use(self, name):
        """
        Resolve one spelling as a syntax use

        :param name: the spelling as written
        :return: Compiled instance, or None
        """

        # prefix-only resolution: the short name is the use; the
        # prefixed spelling denotes the transformer object itself and
        # never resolves as a syntax use
        if name.startswith("compile/"):
            return None
        return self._find_compiled("compile/" + name)


    def find_bare_compiled(self, name):
        """
        Resolve as :meth:`find_compiled`, keeping only a Compiled that
        declares a bare form. Anything else resolves as not found, so a
        symbol naming an ordinary special still compiles as a variable;
        prefixed spellings never resolve as uses, per find_compiled

        :param name: symbol or string for the compile-time object
        :return: Compiled instance with a bare form, or None
        """

        found = self.find_compiled(name)
        if found is None or getattr(found, "bare", None) is None:
            return None
        return found


    def fragment(self, name=None):
        reso = self._resolution_instance
        if reso is None:
            reso = self._resolution_pragma_class()
        return ByteCodeFragment(
            self, str(name), self.filename, resolution=reso)


    def compile(self, source_obj, *, position=None):
        if isinstance(source_obj, ByteCodeFragment):
            return source_obj

        tmp = self._resolution_instance is None
        if tmp:
            self._resolution_instance = self._resolution_pragma_class()

        try:
            fragment = self.fragment(self.name)
            return self.compile_expr(fragment, source_obj, position)
        finally:
            if tmp:
                self._resolution_instance = None


    def full_compile(self, source_obj, *, position=None):
        tmp = self._resolution_instance is None
        if tmp:
            self._resolution_instance = self._resolution_pragma_class()
        try:
            res = source_obj
            seen = None
            while not isinstance(res, ByteCodeFragment):
                if seen is None:
                    seen = [res]
                else:
                    self.check_rewrite_cycle(seen, res)
                    seen.append(res)
                res = self.compile(res, position=position)
            return res
        finally:
            if tmp:
                self._resolution_instance = None


    def check_rewrite_cycle(self, seen, form):
        """
        Guard the full_compile rewrite loop: a rewrite that repeats an
        earlier form in this position is a macro-expansion cycle, and a
        chain past the settling limit is divergent growth; either way
        the compile stops here instead of spinning.

        Identity is exempt from the cycle check: a special may mutate
        its source in place and return the same object to request a
        recompile (the return/yield keyword-values fixup), which is
        progress, not a cycle. A genuinely stuck identical form falls
        to the settling limit instead.

        The settling limit comes from the compiler/rewrite-limit
        pragma setting; 0 disables the limit while the cycle check
        stays in force.

        :param seen: prior rewrite forms in this position, oldest first
        :param form: the newly produced rewrite
        :raises CompilerError: on a cycle or an unsettled chain
        """

        # each expansion marks its output afresh, so a cycle repeats a
        # form up to its marks
        bare = strip_marks(form)
        for i, prior in enumerate(seen):
            if form is prior:
                continue
            if bare == strip_marks(prior):
                chain = " -> ".join(str(strip_marks(f)) for f in seen[i:])
                raise CompilerError(
                    f"macro expansion cycle: {chain} -> {bare}",
                    form)

        limit = self.rewrite_limit
        if limit and len(seen) >= limit:
            raise CompilerError(
                f"macro expansion did not settle after {limit}"
                f" rewrites; last form: {form}",
                form)


    def compile_expression(self, source_obj):
        """
        Compile one top-level source expression: the entry point for the
        places a compilation turn starts -- the module loaders, the REPL,
        the serializer, and any harness whose pragma should govern the
        turn. Binds this compiler's pragma into the active-compiler-pragma
        parameter for the turn's extent, so macro bodies running during
        the compile read the compiling module's pragma through
        (active-pragma), regardless of which module defined them. Inner
        recursive compilation chains through compile and full_compile
        directly and never rebinds.

        :param source_obj: top-level source form
        :return: compiled ByteCodeFragment
        """

        # a turn starts with no open scopes, whatever compilation a macro
        # body may have started this one from
        saved, self.lexical_stack = self.lexical_stack, []
        try:
            with parameterize(**{ACTIVE_PRAGMA_PARAM: self.pragma}):
                return self.full_compile(source_obj)
        finally:
            self.lexical_stack = saved


    def compile_expr(self, fragment, source_obj, position=None):
        """
        ``position`` is the innermost enclosing source position known to
        the caller, threaded to diagnostics about positionless atoms; a
        source object carrying its own position wins over it
        """

        # a marked keyword is the keyword: only a symbol binds or resolves
        if is_mark(source_obj) and is_keyword(source_obj.datum):
            source_obj = source_obj.datum

        if is_symbol(source_obj) or is_mark(source_obj):
            dispatch = self.compile_symbol
        elif is_pair(source_obj):
            dispatch = self.compile_pair
        elif _is_const(source_obj):
            if is_keyword(source_obj):
                self.check_keyword_literal(source_obj)
            dispatch = self.compile_const
        else:
            raise CompilerError("Unsupported source object", source_obj)

        try:
            # every dispatch returns the fragment it emitted into or a
            # rewritten form for the caller to compile in its place; None
            # is that form when a special or macro expands to the
            # constant None, and compiles as such on the next pass
            return dispatch(fragment, source_obj, position)
        except CompilerError:
            raise
        except Exception as ex:
            from ..exceptions import KoskariSyntaxError
            if isinstance(ex, KoskariSyntaxError):
                raise
            raise UncaughtCompilerError(ex, source_obj) from ex


    def compile_const(self, fragment, value, position=None):
        fragment.instruction(OpCode.CONST, value)
        return fragment


    def compile_symbol(self, fragment, sym, position=None):

        bare = strip_mark(sym)
        if bare is not sym:
            fragment.resolution.note_mark(sym)

        if bare is _symbol_colon:
            self.check_stray_colon(bare)

        # TODO: we may want to add an alias/FOO form for
        # compile-time symbolic aliases. These would be distinct
        # from compile/FOO forms which compile over their full
        # source list -- the aliases would only expand the symbol
        # itself.

        if bare in _COMPILER_LITERALS:
            return self.compile_const(fragment, _COMPILER_LITERALS[bare])

        comp = self.find_bare_compiled(sym)
        if comp is not None:
            # the atom is interned and positionless; hand the bare arm
            # the caller-threaded position via the Datum carrier
            return self.compile_special(
                fragment, comp, Datum(sym, position))

        # This is handled by the reader macros now.
        # ex = sym.rsplit(".", 1)
        # if len(ex) > 1:
        #     source = cons(_symbol_attr, *ex, nil)
        #     return self.compile_pair(fragment, source)

        return self.compile_raw_symbol(fragment, sym)


    def compile_raw_symbol(self, fragment, sym):
        strsym = str(sym)
        fragment.request_var(sym)
        fragment.instruction(OpCode.VAR, strsym)
        return fragment


    def _check_bracket_call(self, source_obj):
        """
        Diagnose a bracket-pair reaching call/operator position per the
        bracket_calls policy. The empty list never arrives here (handled as
        a const), so this only fires on a non-empty [ ] form being compiled
        as a combination or special.
        """

        if (self.bracket_calls is BracketCalls.IGNORE
                or not isinstance(source_obj, bracket_pair)):
            return

        msg = ("bracket-pair [ ] used in call position;"
               " use ( ) for calls and syntax forms")

        if self.bracket_calls is BracketCalls.ERROR:
            raise CompilerError(msg, source_obj)

        pos = get_position(source_obj)
        self._source_warning(msg, pos[0] if pos else 0)


    def check_stray_colon(self, sym, pos=None):
        """
        Diagnose a bare ``:`` symbol compiled as a name per the stray_colon
        policy. Nothing binds ``:``, so reaching here is always a mistake --
        usually a layout form half-converted to s-expressions, where the
        trailing block marker of ``(for-each [x (xs)]: body)`` reads as a
        symbol element rather than as a block colon.

        Symbols are interned and so carry no position of their own; ``pos``
        is the enclosing form's ``(line, col)``, used to attribute a WARN.
        It defaults to the innermost form compiled so far.

        :param sym: the symbol about to be compiled as a name
        :param pos: position of the enclosing form, or None
        """

        if (sym is not _symbol_colon
                or self.stray_colon is StrayColon.ALLOW):
            return

        if pos is None:
            pos = self._last_position

        msg = ("stray ':' symbol compiled as a name; a layout block colon"
               " has no meaning inside an s-expression form")

        if self.stray_colon is StrayColon.ERROR:
            raise CompilerError(msg, sym)

        self._source_warning(msg, pos[0] if pos else 0)


    def check_keyword_literal(self, kwd, pos=None):
        """
        Diagnose an unquoted keyword compiled as a literal value per the
        keyword_literals policy. It evaluates to itself, which is legal but
        is far more often a typo -- ``(if foo: 1 2)`` tests the keyword
        ``foo:`` for truthiness (always true) instead of reading ``foo``.

        Quoting is exempt: the quote special emits its own constant and
        never reaches here. Keyword arguments are exempt too, being
        consumed structurally by gather_parameters rather than as values.

        Keywords are interned and so carry no position of their own; ``pos``
        is the enclosing form's ``(line, col)``, used to attribute a WARN.
        It defaults to the innermost form compiled so far.

        :param kwd: the keyword about to be compiled as a constant
        :param pos: position of the enclosing form, or None
        """

        if self.keyword_literals is KeywordLiterals.ALLOW:
            return

        if pos is None:
            pos = self._last_position

        # str() on a keyword drops the colon; kos_repr keeps it, so the
        # suggested quoted form is one the reader would actually accept
        shown = kos_repr(kwd)
        msg = (f"keyword {shown} used as a literal value;"
               f" quote it ('{shown}) if that is intended")

        if self.keyword_literals is KeywordLiterals.ERROR:
            raise CompilerError(msg, kwd)

        self._source_warning(msg, pos[0] if pos else 0)


    def _source_warning(self, msg, lineno):
        """
        Emit a SyntaxWarning attributed to the koskari source location
        rather than to this compiler frame, so the output isn't
        compiler-internal noise (no compiler.py path, no echoed
        warnings.warn() line).
        """

        warnings.warn_explicit(msg, SyntaxWarning,
                               self.filename or "<unknown>", lineno)


    def compile_pair(self, fragment, source_obj, position=None):
        # print("compile_pair", kos_repr(source_obj))

        if is_nil(source_obj):
            return self.compile_const(fragment, source_obj)

        self._check_bracket_call(source_obj)

        pos = get_position(source_obj)
        if pos:
            # retained so a diagnostic about a positionless source object
            # (an interned symbol) can name the innermost enclosing form
            self._last_position = pos
            fragment.position(pos)

        if is_proper(source_obj):
            head, tail = source_obj
        else:
            # we're going to treat improper lists as the datatype itself,
            # but still evaluate the internal elements (so not quoted)
            return cons(_symbol_cons, *source_obj.unpack(), nil)

        if is_symbol(strip_mark(head)):
            comp = self.find_compiled(head)
            if comp:
                return self.compile_special(fragment, comp, source_obj)

            if strip_mark(head) is _symbol_call:
                raise CompilerError("compile/call not found", source_obj)

        elif is_pair(head):
            # expansion is legal in operator position: when the head is
            # itself a form whose own head is a syntax, attempt to lower
            # it. A successful lowering re-emits this form with the new
            # head for a fresh dispatch; a failed attempt commits nothing
            # and the raw head flows to the call path below.
            lowered = self.lower_head(head, position=pos or position)
            if lowered is not None:
                ret = cons(lowered, tail)
                fill_position(ret, pos or position)
                return ret

        # Re-emit as an explicit call form with the head left as raw
        # source. Compiling the head here would destroy forms that
        # compile/call's patterns need to see (eg. a (compile/attr obj
        # name) head routing to compile/call-attr); the special compiles
        # the callable itself when no pattern claims it.
        reemitted = cons(_symbol_call, head, tail)
        fill_position(reemitted, pos or position)
        return reemitted


    def lower_head(self, form, position=None):
        """
        Attempt to lower a form found in operator position.

        The attempt nests through arbitrarily deep pair heads: each level
        resolves its own head, and only a form whose innermost head names
        a syntax is attempted at all. A syntax whose expansion yields a
        source form succeeds, reconstructing each enclosing level around
        the result; a non-syntax head, or an expansion that compiles to
        bytecode, fails the whole attempt. Failure commits nothing -- the
        discarded fragment was never merged -- so the caller's raw form
        flows to the call path untouched.

        :param form: the pair found in operator position
        :param position: enclosing source position threaded to diagnostics
        :return: the lowered replacement form, or None when the attempt
            fails
        """

        head = form.car

        if is_pair(head):
            inner = self.lower_head(head, position)
            if inner is None:
                return None
            ret = cons(inner, form.cdr)
            fill_position(ret, get_position(form) or position)
            return ret

        if not is_symbol(strip_mark(head)):
            return None

        comp = self.find_compiled(head)
        if comp is None:
            return None

        res = comp.compile_impl(self, form)
        if res is None or isinstance(res, ByteCodeFragment):
            return None
        return res


    def compile_special(self, fragment, comp, source_obj):
        res = comp.compile_impl(self, source_obj)
        if isinstance(res, ByteCodeFragment):
            fragment.merge(res)
            return fragment
        return res


    def complete(
            self,
            fragment,
            mode=None,
            _validate=True,
            is_root=False,
            ancestors=None):
        """
        Complete a fragment into a Code object.

        The function bodies the fragment holds as DeferredFunction
        constants complete first, against this fragment's now final
        declarations, and their FunctionSpecs take their place

        :param fragment: the fragment to complete
        :param mode: the mode to complete in
        :param _validate: whether to validate the code
        :param is_root: whether this is the root fragment
        :param ancestors: the fragments lexically enclosing this one,
            outermost first; the open lexical stack when omitted
        :return: the completed Code object
        """

        reso = fragment.resolution

        if ancestors is None:
            ancestors = tuple(self.lexical_stack)

        self.complete_deferred(fragment)

        if reso.marks:
            resolve_marks(fragment, ancestors)

        # handles compression, var-to-fast lowering, and tail-call
        # promotion, deriving the pass settings from this compiler
        # through the fragment; the base NamespaceSpec it resolves
        # mid-pipeline stays readable for Code construction below
        proc = ByteCodeProcessor(fragment)
        instr = proc.process(mode == CompilerMode.FUNCTION)
        base_nspec = proc.base_nspec

        # Resolve filename for Code metadata
        try:
            path = self.module.__path__
        except AttributeError:
            path = None
        filename = (fragment.filename or path or self.filename or None)

        # Gate per-site opcode caches by compile context: function/lambda
        # bodies follow compiler/cache-function (baseline on), everything else
        # (module/REPL top-level expressions) follows compiler/cache-expression
        # (baseline off). When off, the code object allocates no cache slots.
        if mode == CompilerMode.FUNCTION:
            cache = self.cache_function
        else:
            cache = self.cache_expression

        # Build Code and register with pragma for position lookup on errors
        code = Code(base_nspec, instr, fragment.name, filename,
                    protect=self.protect_code, cache=cache)
        if fragment.doc is not None:
            code.extras['doc'] = fragment.doc
        reso.register_code_fragment(code, fragment, ancestors)

        # Recursively validate that all writes are satisfied.
        # Only validate from root complete(); inner complete() calls skip so
        # nested functions get validated with proper ancestor context.
        if _validate and is_root:
            reso.validate_tree(
                code, (), CompilerMode.FUNCTION, is_root=True)

        # finally, register the code with the minted registry so that it
        # can potentially be sealed later. A compiler kept past its seal
        # (one rebuilt by get-compiler) is never drained, so shed the
        # dead entries now and then to keep the registry bounded.
        if len(self._minted) % 1024 == 1023:
            self._minted = [r for r in self._minted if r() is not None]
        self._minted.append(weakref.ref(code))

        return code


    def complete_deferred(self, fragment):
        """
        Complete every function body held as a DeferredFunction constant
        in the fragment's instruction stream and replace each with its
        FunctionSpec

        :param fragment: the fragment about to complete
        """

        rows = fragment.instructions
        for i, (op, arg) in enumerate(rows):
            if op is OpCode.CONST and isinstance(arg, DeferredFunction):
                # a function fragment was on the lexical stack while its
                # body compiled; the root never is, and encloses the rest
                ancestors = arg.ancestors
                if fragment not in ancestors:
                    ancestors = (fragment,) + ancestors
                code = self.complete(
                    arg.fragment, CompilerMode.FUNCTION, ancestors=ancestors)
                rows[i] = (op, FunctionSpec(arg.name, arg.formals_spec, code))


    def seal(self):
        """
        Pack this module's surviving code objects into sealed arenas.

        Drains the minted registry, keeping the code objects that are
        still alive and not already packed -- reachability from the
        module namespace is what makes a code object durable. Survivors
        are partitioned in definition order per the compiler/pack-module
        caps, each segment is relocated into a shared Arena, and each
        arena is sealed read-only with a single mprotect.

        Idempotent: a second call finds an empty registry, so a
        long-lived compiler (an interactive session's, or the bootstrap
        loop's) may seal repeatedly, packing only what accumulated since
        the previous seal. Must run at a quiescent point (module body
        evaluation complete, no frame of the target code mid-dispatch);
        the end of a module load satisfies this.

        :return: tuple of the Arena objects produced; empty when packing
            is disabled or nothing survived
        """

        refs, self._minted = self._minted, []

        pack = self.pack_module
        if not (pack and pack.enabled):
            return ()

        survivors = [c for c in (r() for r in refs)
                     if c is not None and c.arena is None]
        if not survivors:
            return ()

        # Partition in definition order. Either cap starting a new
        # segment; a single oversize member still lands alone rather
        # than failing to place.
        segments = []
        current = []
        current_bytes = 0
        for code in survivors:
            size = _arena_member_size(code)
            if current and (
                    (pack.max_count
                     and len(current) >= pack.max_count)
                    or (pack.max_bytes
                        and current_bytes + size > pack.max_bytes)):
                segments.append((current, current_bytes))
                current = []
                current_bytes = 0
            current.append(code)
            current_bytes += size
        segments.append((current, current_bytes))

        arenas = []
        for codes, total in segments:
            arena = Arena(total)
            for code in codes:
                code.pack_into(arena)
            arena.seal()
            arenas.append(arena)

        return tuple(arenas)


# The end.
