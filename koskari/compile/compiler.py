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
from abc import ABC, abstractmethod
from contextlib import contextmanager
from enum import IntEnum, auto

from ..exceptions import CompilerError, UncaughtCompilerError
from ..hygiene import Deferred, Resolver, candidates, equal_by_base
from ..lib import (
    Code, CodeAllocator, Datum, FormalsSpec, FunctionSpec, Module,
    ModuleNamespace, Namespace, NamespaceSpec, OpCode, OpStyle, bracket_pair,
    cons, kos_repr, nil, symbol)
from ..types import (
    fill_position, get_position, is_const, is_keyword, is_nil, is_pair,
    is_proper, is_symbol, parameterize)
from .pragma import (
    BracketCalls, KeywordLiterals, KoskariPragma, ResolutionMode, StrayColon,
    StrayHash)
from .pragma.settings import CODE_PROTECTION
from .processor import ByteCodeProcessor, merge_function_constant_needs


class Compiled(ABC):

    @abstractmethod
    def compile_impl(self, compiler, source_obj):
        """
        Compile a source object into a bytecode fragment
        """

        ...


def _label_generator(formatstr="label_%04x"):
    counter = 0

    def gen_label(text=None):
        nonlocal counter
        counter += 1
        if text is None:
            return formatstr % counter
        return f"{text}_{counter:x}"

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
_symbol_hash = symbol("#")

# the parameter name (active-pragma) reads; the compiler binds it for
# each compilation turn
ACTIVE_PRAGMA_PARAM = "active-compiler-pragma"


class ByteCodeFragment:
    """
    Holds a fragment of bytecode during compilation: instructions plus
    closure requests and name declarations for the namespace
    """

    # raised when the fragment references a marked name, directly or
    # through a fragment it absorbs or a function body it defers
    marked = False


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

        # set by a named lambda: the symbol it binds itself under, marks
        # and all; None for any other fragment
        self.self_ref = None

        # the source position of the first write, the first declaration
        # (define or define-global) and the first read request of each
        # name, carried along by merge so the fragment that is validated
        # can locate the form that fails
        self.write_positions = {}
        self.declare_positions = {}
        self.request_positions = {}

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

        # set by function_constant when this fragment receives a
        # deferred function: that function's requests reach this one
        # only when it completes, so this one cannot complete early
        self.holds_deferred = False


    def as_namespace_spec(self):
        """
        Build a NamespaceSpec from this fragment's name and declarations.
        """

        return self.resolution.build_namespace_spec(self)


    def note_mark(self, sym):
        """
        Record that this fragment references a marked name, so its
        completion resolves its names

        :param sym: a gensym
        """

        self.marked = True


    def request_var(self, var_name, position=None):
        """
        Record a closure request for var_name unless it is already declared

        :param var_name: name to request from enclosing scope
        :param position: optional (line, col) for error reporting
        """

        if not isinstance(var_name, symbol):
            raise TypeError(f"expected a symbol, not {var_name!r}")
        if var_name.base is not var_name:
            self.note_mark(var_name)
        self.resolution.on_request_var(self, var_name, position=position)
        if position is not None:
            self.request_positions.setdefault(var_name, position)


    def request_for_write(self, var_name, position=None):
        """
        Record that var_name must be writable (setq, delq, inplace).

        :param var_name: name that must be writable
        :param position: optional (line, col) for error reporting
        """

        if not isinstance(var_name, symbol):
            raise TypeError(f"expected a symbol, not {var_name!r}")
        if var_name.base is not var_name:
            self.note_mark(var_name)
        if position is not None:
            self.write_positions.setdefault(var_name, position)
        self.resolution.on_write(self, var_name, position=position)


    def request_global(self, var_name, position=None):
        """
        Record explicit need from module (define-global).

        :param var_name: name to pull from module
        :param position: optional (line, col) for error reporting
        """

        if not isinstance(var_name, symbol):
            raise TypeError(f"expected a symbol, not {var_name!r}")
        if var_name.base is not var_name:
            self.note_mark(var_name)
        # the hook runs first so it sees the position of an earlier
        # declaration of the name, never this one's
        self.resolution.on_explicit_global(self, var_name, position=position)
        if position is not None:
            self.declare_positions.setdefault(var_name, position)


    def declare_var(self, var_name, position=None, type_expr=None):
        """
        Record a name declared in this fragment's namespace.

        :param var_name: name to declare
        :param position: optional (line, col) for error reporting
        :param type_expr: optional source form for a type annotation (compile
            metadata only; not evaluated here)
        """

        if not isinstance(var_name, symbol):
            raise TypeError(f"expected a symbol, not {var_name!r}")
        # the hook runs first so it sees the position of an earlier
        # declaration of the name, never this one's
        self.resolution.on_declare(self, var_name, position=position)
        if position is not None:
            self.declare_positions.setdefault(var_name, position)
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
            # the position record stores 16-bit line and column; a
            # source past either saturates rather than wrapping
            line, col = (min(n, 65535) for n in linecol[:2])
            self.instruction(OpCode.POSITION, (line, col, extra))


    def instruction(self, op, opa):
        """
        Append one bytecode instruction

        :param op: OpCode for the instruction
        :param opa: argument for the instruction; a variable operand,
            the opcode's own or a variable field of a fused row, is a
            symbol
        :raises TypeError: when a variable operand is not a symbol
        """

        if op.style == OpStyle.VAR:
            if not isinstance(opa, symbol):
                raise TypeError(f"expected a symbol, not {opa!r}")
        elif op.extra and OpStyle.VAR in op.extra:
            for v, fk in zip(opa, op.extra):
                if fk == OpStyle.VAR and not isinstance(v, symbol):
                    raise TypeError(f"expected a symbol, not {v!r}")

        self.instructions.append((op, opa))


    def rewrite_vars(self, choose):
        """
        Replace each variable operand in the instruction stream with
        choose(name, scopes), scopes holding the spelling sets of the
        inline scopes open around it, innermost first. An inline
        scope's closure names are chosen from outside that scope

        :param choose: callable taking an atom and the open scopes,
            returning the atom to emit
        """

        def rebuild_nspec(nspec, scopes):
            closure = []
            for name in nspec.closure_syms:
                chosen = choose(name, scopes)
                if chosen not in closure:
                    closure.append(chosen)
            if tuple(closure) == tuple(nspec.closure_syms):
                return nspec
            return NamespaceSpec(
                nspec.name, nspec.local_syms, tuple(closure),
                nspec.global_syms)

        def scope_names(nspec):
            # a global an inline scope declares is bound in the module
            # and visible within the scope under that spelling, as its
            # locals are
            return {str(n) for n in nspec.local_syms} | {
                str(n) for n in nspec.global_syms}

        # the stream is the compiler's own emission: PUSH_NS and
        # PUSH_COLLECT carry their NamespaceSpec as the operand, and the
        # fused rows the processor mints later are not here yet
        scopes = []
        out = []
        for op, arg in self.instructions:
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

        self.instructions = out


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
            self.declare_var(
                name, position=other.declare_positions.get(name))
        for name in other.requests:
            self.request_var(
                name, position=other.request_positions.get(name))
        for name in other.requests_for_write:
            self.request_for_write(
                name, position=other.write_positions.get(name))
        for name in other._request_global:
            self.request_global(
                name, position=other.declare_positions.get(name))
        self.annotation_bindings.extend(other.annotation_bindings)
        if other.marked:
            self.marked = True
        if other.holds_deferred:
            self.holds_deferred = True
        self.instructions.extend(other.instructions)


    @contextmanager
    def child_namespace(self, ns_name, names, values):
        """
        Context manager that compiles a child scope with bound names.
        On entry, compiles all value expressions and yields a fragment for
        the body. On exit, emits value expressions (if any), then PUSH_NS with
        nspec and names in the EXTRA row, body instructions, POP_NS.

        :param ns_name: name for the child namespace (e.g. source form)
        :param names: sequence of bound names, symbols
        :param values: sequence of expressions (one per name)
        """

        compiler = self.compiler
        ns_name = str(ns_name)
        names = tuple(names)
        if len(names) != len(values):
            raise CompilerError(
                "mismatch between names and values",
                (names, values))

        # the values run before PUSH_NS, in the enclosing scope, so they
        # compile there too: before the child scope opens and before its
        # names are declared
        valfrag = None
        if names:
            valfrag = compiler.fragment(ns_name)
            for val in values:
                valfrag.merge(compiler.full_compile(val))

        child = compiler.fragment(ns_name)
        with compiler.resolution_scope(child):
            for name in names:
                child.declare_var(name)

            yield child
            # a function built inside the scope closes over its names
            # through the scope's namespace, so the scope must capture
            # them before its spec is built
            merge_function_constant_needs(child, child.instructions)
            nspec = child.as_namespace_spec()
            child.resolution.register_scope_fragment(
                child, tuple(f for f in compiler.lexical_stack if f is not child))

            # A name the scope closes over is a closure need of the
            # enclosing scope as well: the enclosing function captures
            # the cell in its own slot at creation, and the scope's
            # PUSH_NS resolves it from the frame. A namespace's parent
            # link skips call frames, so the frame is the only place
            # the scope can reach it.
            for name in child.resolution.closure_names(child):
                if name not in self.declares:
                    self.request_var(
                        name, position=child.request_positions.get(name))

            if names:
                self.merge(valfrag)

            # the bound names stay atoms until lowering spells them
            self.instruction(
                OpCode.PUSH_NS, (nspec, names if names else None, 0, 0))
            if child.marked:
                self.marked = True
            if child.holds_deferred:
                self.holds_deferred = True
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
            merge_function_constant_needs(child, child.instructions)
            nspec = child.as_namespace_spec()
            child.resolution.register_scope_fragment(
                child, tuple(f for f in compiler.lexical_stack if f is not child))
            for name in child.resolution.closure_names(child):
                if name not in self.declares:
                    self.request_var(
                        name, position=child.request_positions.get(name))

            self.instruction(OpCode.PUSH_COLLECT, nspec)
            if child.marked:
                self.marked = True
            if child.holds_deferred:
                self.holds_deferred = True
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
            stray_hash=StrayHash.ALLOW,
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
        self.stray_hash = stray_hash
        self.keyword_literals = keyword_literals
        self.protect_code = False
        self.pack_module = CODE_PROTECTION
        self.cache_expression = False
        self.cache_function = True
        self.rewrite_limit = 32

        # position of the innermost form compiled so far, for attributing
        # diagnostics about source objects that carry no position
        self._last_position = None
        # (head form, fragment) from the latest lowering attempt that
        # compiled its head to bytecode, taken by the next compile of
        # that head
        self._compiled_head = None

        # the CodeAllocator function code is placed into while a module
        # load is open; None outside a load, where every Code is a heap
        # block
        self.allocator = None

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

        # a ModuleNamespace is a dict subclass, so the namespace kinds
        # are checked ahead of a plain dict of builtins
        if module is None:
            module = Module(ModuleNamespace(name, {}))
        elif isinstance(module, (Namespace, ModuleNamespace)):
            module = Module(module)
        elif isinstance(module, dict):
            module = Module(ModuleNamespace(name, module))

        self.module = module

        self.gen_label = _label_generator()

        ns = vars(module)

        # stored through the namespace, not its Ref, so a module
        # namespace mirrors __name__ where CPython reads it raw
        current = ns.get("__name__")
        if current is None:
            ns.store("__name__", name)
        elif name is None:
            name = current
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
        self._stray_hash = pragma.get("compiler/stray-hash")
        self._keyword_literals = pragma.get("compiler/keyword-literals")

        self.protect_code = pragma.get("compiler/protect-code", False)
        self.pack_module = pragma.get("compiler/pack-module", CODE_PROTECTION)

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
    def stray_hash(self):
        return self._stray_hash


    @stray_hash.setter
    def stray_hash(self, value):
        if not isinstance(value, StrayHash):
            value = StrayHash(value)
        self._stray_hash = value


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
        depth = len(self._macro_overlay)
        self.scope_overlay(name, syntax)
        try:
            yield
        finally:
            # a scope_overlay made inside the extent goes with it
            del self._macro_overlay[depth:]


    def scope_overlay(self, name, syntax):
        """
        Overlay the macro namespace with one definition until the
        innermost open resolution scope, or the enclosing macro_overlay
        extent, closes

        :param name: the binding spelling, compile/NAME
        :param syntax: the Compiled object to resolve it to
        """

        if self._macro_overlay:
            dfns = dict(self._macro_overlay[-1])
            dfns[str(name)] = syntax
        else:
            dfns = {str(name): syntax}
        self._macro_overlay.append(dfns)


    @contextmanager
    def macro_overlays(self, **dfns):
        """
        Context manager that overlays the macro namespace with the given
        definitions.
        """

        depth = len(self._macro_overlay)
        if self._macro_overlay:
            dfns = dict(self._macro_overlay[-1], **dfns)
        self._macro_overlay.append(dfns)
        try:
            yield
        finally:
            del self._macro_overlay[depth:]


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

        overlay_depth = len(self._macro_overlay)
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
            # a syntax the body defined is scoped to the body
            del self._macro_overlay[overlay_depth:]


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
        if not is_symbol(name):
            return self._find_compiled_use(str(name))

        for cand in candidates(name):
            found = self._find_compiled_use(str(cand))
            if found is not None:
                return found
        return None


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


    def symbol_is_variable(self, sym):
        """
        Whether a lone symbol compiles to a variable read, so a special
        may name it in a fused VAR operand: it is not a compiler literal
        and no bare arm expands it. Anything else must go through
        full_compile, which expands the bare arm or emits the constant

        :param sym: the symbol as written, marks included
        """

        if sym.base in _COMPILER_LITERALS:
            return False
        return self.find_bare_compiled(sym) is None


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
        # a code is named by the plain name: a symbol's expansion mark
        # belongs to its binding, never to the text
        if is_symbol(name):
            name = name.base
        return ByteCodeFragment(
            self, str(name), self.filename, resolution=reso)


    def compile(self, source_obj, *, position=None):
        if isinstance(source_obj, ByteCodeFragment):
            return source_obj

        # a head the lowering attempt compiled: the call path compiles
        # the same object next, and takes the fragment in its place
        if self._compiled_head is not None \
                and self._compiled_head[0] is source_obj:
            fragment = self._compiled_head[1]
            self._compiled_head = None
            return fragment

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
                # a rewrite built without a position is compiled under
                # the position of the form it replaced
                if is_pair(res):
                    position = get_position(res) or position
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
        # form up to its marks; str of a form already prints the bases
        for i, prior in enumerate(seen):
            if form is prior:
                continue
            if equal_by_base(form, prior):
                chain = " -> ".join(str(f) for f in seen[i:])
                raise CompilerError(
                    f"macro expansion cycle: {chain} -> {form}",
                    form)

        limit = self.rewrite_limit
        if limit and len(seen) >= limit:
            raise CompilerError(
                f"macro expansion did not settle after {limit}"
                f" rewrites; last form: {form}",
                form)


    def compile_expression(self, source_obj, position=None):
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
        :param position: where the reader found the form, for a form
            that carries no position of its own (an atom)
        :return: compiled ByteCodeFragment
        """

        # a turn starts with no open scopes, whatever compilation a macro
        # body may have started this one from
        saved, self.lexical_stack = self.lexical_stack, []
        try:
            with parameterize(**{ACTIVE_PRAGMA_PARAM: self.pragma}):
                return self.full_compile(source_obj, position=position)
        finally:
            self.lexical_stack = saved


    def compile_expr(self, fragment, source_obj, position=None):
        """
        ``position`` is the innermost enclosing source position known to
        the caller, threaded to diagnostics about positionless atoms; a
        source object carrying its own position wins over it
        """

        if is_symbol(source_obj):
            dispatch = self.compile_symbol
        elif is_pair(source_obj):
            dispatch = self.compile_pair
        elif is_const(source_obj):
            if is_keyword(source_obj):
                self.check_keyword_literal(source_obj)
            dispatch = self.compile_const
        else:
            raise CompilerError("Unsupported source object", source_obj,
                                position=position, filename=self.filename)

        try:
            # every dispatch returns the fragment it emitted into or a
            # rewritten form for the caller to compile in its place; None
            # is that form when a special or macro expands to the
            # constant None, and compiles as such on the next pass
            return dispatch(fragment, source_obj, position)
        except CompilerError as ex:
            # the innermost compile_expr sees the error first, so a
            # position taken here is the innermost enclosing form's
            if is_pair(source_obj):
                position = get_position(source_obj) or position
            ex.attach_position(position, self.filename)
            raise
        except Exception as ex:
            from ..exceptions import KoskariSyntaxError
            if isinstance(ex, KoskariSyntaxError):
                raise
            raise UncaughtCompilerError(ex, source_obj) from ex


    def compile_const(self, fragment, value, position=None):
        self.position_atom(fragment, position)
        fragment.instruction(OpCode.CONST, value)
        return fragment


    def position_atom(self, fragment, position):
        """
        An atom carries no position; when it is the whole of what the
        fragment holds so far, the position threaded from the reader
        names its line
        """

        if position and not fragment.instructions:
            fragment.position(position)


    def compile_symbol(self, fragment, sym, position=None):

        self.position_atom(fragment, position)

        bare = sym.base
        if bare is not sym:
            fragment.note_mark(sym)

        if bare is _symbol_colon or bare is _symbol_hash:
            self.check_stray_symbol(bare)

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

        return self.compile_raw_symbol(fragment, sym, position)


    def compile_raw_symbol(self, fragment, sym, position=None):
        fragment.request_var(sym, position=position)
        fragment.instruction(OpCode.VAR, sym)
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


    def check_stray_symbol(self, sym, pos=None):
        """
        Diagnose a bare ``:`` or ``#`` symbol compiled as a name per the
        stray_colon or stray_hash policy. Nothing binds either, so reaching
        here is always a mistake. A stray ``:`` is usually a layout form
        half-converted to s-expressions, where the trailing block marker of
        ``(for-each [x (xs)]: body)`` reads as a symbol element rather than
        as a block colon. A stray ``#`` is usually a typo for a collection
        opener or a syntax quote.

        Symbols are interned and so carry no position of their own; ``pos``
        is the enclosing form's ``(line, col)``, used to attribute a WARN.
        It defaults to the innermost form compiled so far.

        :param sym: the symbol about to be compiled as a name
        :param pos: position of the enclosing form, or None
        """

        if sym is _symbol_colon:
            level = self.stray_colon.value
            msg = ("stray ':' symbol compiled as a name; a layout block"
                   " colon has no meaning inside an s-expression form")
        elif sym is _symbol_hash:
            level = self.stray_hash.value
            msg = ("stray '#' symbol compiled as a name; '#' prefixes a"
                   " collection opener or a syntax quote")
        else:
            return

        if level == 'allow':
            return

        if pos is None:
            pos = self._last_position

        if level == 'error':
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

        if is_symbol(head):
            comp = self.find_compiled(head)
            if comp:
                return self.compile_special(fragment, comp, source_obj)

            if head.base is _symbol_call:
                raise CompilerError("compile/call not found", source_obj)

        elif is_pair(head):
            # expansion is legal in operator position: when the head is
            # itself a form whose own head is a syntax, attempt to lower
            # it. A successful lowering re-emits this form with the new
            # head for a fresh dispatch; a failed attempt commits nothing
            # and the raw head flows to the call path below. A head that
            # compiled to bytecode is remembered, so the call path's
            # compile of the same head answers with that fragment.
            lowered = self.lower_head(head, position=pos or position)
            if isinstance(lowered, ByteCodeFragment):
                self._compiled_head = (head, lowered)
            elif lowered is not None:
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
        Attempt to lower a form found in operator position. Caller:
        compile_pair.

        The attempt nests through arbitrarily deep pair heads: each level
        resolves its own head, and only a form whose innermost head names
        a syntax is attempted at all. A syntax whose expansion yields a
        source form succeeds, reconstructing each enclosing level around
        the result; one that compiles to bytecode answers with that
        fragment, which the caller remembers against the head so the
        call path's compile of the same head is that fragment and the
        head is never compiled twice. A non-syntax head fails the whole
        attempt, committing nothing.

        :param form: the pair found in operator position
        :param position: enclosing source position threaded to diagnostics
        :return: the lowered replacement form, a fragment the head
            compiled to, or None when the attempt fails
        """

        head = form.car

        if is_pair(head):
            inner = self.lower_head(head, position)
            if inner is None:
                return None
            ret = cons(inner, form.cdr)
            fill_position(ret, get_position(form) or position)
            return ret

        if not is_symbol(head):
            return None

        comp = self.find_compiled(head)
        if comp is None:
            return None

        # a head that compiles to bytecode is kept as that fragment and
        # becomes the callee: compiling it again from the raw head would
        # double the work at every nesting level
        return comp.compile_impl(self, form)


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

        The Deferred constants the fragment holds complete first,
        against this fragment's now final declarations, and their
        values take their place

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

        rows = fragment.instructions
        for i, (op, arg) in enumerate(rows):
            if op is OpCode.CONST and isinstance(arg, Deferred):
                rows[i] = (op, arg.complete(self, fragment, ancestors))

        if fragment.marked:
            Resolver(fragment, ancestors, vars(self.module)).resolve(fragment)

        # handles compression, var-to-fast lowering, and tail-call
        # promotion, deriving the pass settings from this compiler
        # through the fragment; the base NamespaceSpec it resolves
        # mid-pipeline stays readable for Code construction below
        try:
            proc = ByteCodeProcessor(fragment)
            instr = proc.process(mode == CompilerMode.FUNCTION)
        except (ValueError, OverflowError) as ex:
            raise self.limit_error(fragment, ex) from ex
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

        # Function code minted during a module load is placed into the
        # load's arena chunks, where it stays; a sealing load seals the
        # chunks when it completes. Expression code is always a heap block
        allocator = None
        if mode == CompilerMode.FUNCTION and self.pack_module:
            allocator = self.allocator

        # Build Code and register with pragma for position lookup on errors
        try:
            code = Code(base_nspec, instr, fragment.name, filename,
                        protect=self.protect_code, cache=cache,
                        allocator=allocator, self_slot=proc.self_slot)
        except (ValueError, OverflowError) as ex:
            raise self.limit_error(fragment, ex) from ex
        if fragment.doc is not None:
            code.extras['doc'] = fragment.doc
        reso.register_code_fragment(code, fragment, ancestors)

        # Recursively validate that all writes are satisfied.
        # Only validate from root complete(); inner complete() calls skip so
        # nested functions get validated with proper ancestor context.
        if _validate and is_root:
            reso.validate_tree(
                code, (), CompilerMode.FUNCTION, is_root=True)
        if is_root:
            reso.close()

        return code


    def limit_error(self, fragment, ex):
        """
        A size the processor or the finalizer refused, reported as a
        compiler error at the fragment's first recorded position
        """

        position = None
        for op, arg in fragment.instructions:
            if op is OpCode.POSITION:
                position = tuple(arg[:2])
                break
        return CompilerError(str(ex), position=position,
                             filename=self.filename)


    def complete_function(self, fragment, ancestors):
        """
        Complete a function body fragment under the given ancestry

        :param fragment: the function body fragment
        :param ancestors: the fragments enclosing it, outermost first
        :return: the completed Code object
        """

        return self.complete(
            fragment, CompilerMode.FUNCTION, ancestors=ancestors)


    def open_allocator(self):
        """
        Open a CodeAllocator for a module load, unless one is already
        open. Function code completed from here until the next seal is
        placed into its arena chunks while compiler/pack-module is on

        :return: the open CodeAllocator
        """

        if self.allocator is None:
            self.allocator = CodeAllocator()
        return self.allocator


    def seal(self):
        """
        Seal the arena chunks this module load placed function code
        into, read-only, and close the load's allocator. Code completed
        afterwards is a heap block until a later load opens another.

        Idempotent: with no allocator open, there is nothing to seal.
        Nothing moves, so sealing is safe at any point between two
        completions.

        :return: tuple of the sealed Arena chunks; empty when no
            allocator was open or nothing was placed
        """

        if self.allocator is None:
            return ()
        chunks = self.allocator.seal()
        self.allocator = None
        return chunks


# The end.
