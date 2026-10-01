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
koskari.compile.pragma.resolution

Resolution pragma protocol and implementations: Koskari, Scheme, Python modes.
Pluggable name-resolution semantics for the compiler.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from typing import Optional, Protocol, Tuple

from ...exceptions import CompilerError
from ...lib import FunctionSpec, NamespaceSpec, symbol


_LOCAL_GLOBAL_CONFLICT = (
    "%r cannot be both local (define) and global (define-global).")


def _unique_append(lst, val):
    """
    Append val to lst only if not already present
    """

    # We don't worry about the 0/False/1/True overlap in this case,
    # because we're just using it for ordered name collecting
    if val not in lst:
        lst.append(val)


def _in_module_scope(fragment):
    """
    Whether fragment compiles at module scope: it is no frame or inline
    scope itself and none is open around it, so its declarations are
    module bindings
    """

    if fragment.function_scope or fragment.collector_scope:
        return False
    compiler = fragment.compiler
    return compiler is None or not compiler.lexical_stack


def _spellings(fragment):
    """
    The spellings of the names a fragment declares or binds globally,
    as a Code spells them
    """

    return [str(n) for n in fragment.declares] + [
        str(n) for n in fragment._request_global]


class ResolutionPragma(Protocol):
    """
    Protocol for resolution semantics during compilation
    """

    def on_write(self, fragment, name, *, position=None):
        """
        Invoked when a write (setq, delq, inplace) is encountered
        """
        ...


    def on_declare(self, fragment, name, *, position=None):
        """
        Invoked when a name is declared (define, params, bindings)
        """
        ...


    def on_explicit_global(self, fragment, name, *, position=None):
        """
        Invoked when a name is explicitly declared global (define-global)
        """
        ...


    def on_request_var(self, fragment, name):
        """
        Invoked when a closure read is requested (name not yet declared)
        """
        ...


    def enter_scope(self):
        """
        Invoked when entering a nested scope (let, lambda, etc.)
        """
        ...


    def exit_scope(self):
        """
        Invoked when exiting a nested scope
        """
        ...


    def closure_names(self, fragment):
        """
        The symbols a fragment closes over
        """
        ...


    def build_namespace_spec(self, fragment) -> NamespaceSpec:
        """
        Build NamespaceSpec from fragment buckets; the names stay atoms
        until lowering spells them
        """
        ...


    def validate_tree(self, code, ancestor_nspecs, mode, is_root=False):
        """
        Recursively validate requests_for_write for nested function Codes
        """
        ...


    def cpool_merge(self, fragment, inner_nspec, inner_write_syms):
        """
        Merge inner Code's closure and write needs into fragment
        """
        ...


    def get_write_position(self, fragment, name: str) -> Optional[Tuple]:
        """
        Return (line, col) for the first write to name in fragment, or None
        """
        ...


    def get_request_for_write_syms(self, code) -> Tuple:
        """
        Return names that require write capability for the given Code.
        Compile-time only; tracked by pragma keyed by Code id.
        """
        ...


class KoskariPragma:
    """
    Strict resolution: explicit declaration required before write.
    Writes to undeclared names raise at validation time.
    """

    def __init__(self):
        # Map (fragment_id, name) -> (line, col) for error reporting
        self._write_positions = {}

        # Map code_id -> source fragment for validation-time position lookup
        self._code_to_fragment = {}

        # Map code_id -> tuple of names requiring write (compile-time only)
        self._code_request_for_write = {}
        # Map code_id -> fragments lexically enclosing the Code, outermost
        # first, recorded when it was completed
        self._code_ancestors = {}
        # Map id(function fragment) -> [(inline scope fragment, ancestors)]
        # for the let and class bodies compiled inline into that
        # function, validated alongside its Code; inline scopes with no
        # enclosing function are module level and skipped like the root
        self._inline_scopes = {}


    def on_write(self, fragment, name, *, position=None):
        """
        Record a write (setq, delq, inplace) to name, a symbol.
        Adds to requests_for_write unless already declared or global.
        """

        # Store first occurrence position for validation error messages,
        # keyed by spelling since validation reads names from a Code
        if position is not None:
            key = (id(fragment), str(name))
            if key not in self._write_positions:
                self._write_positions[key] = position

        if name not in fragment.declares and name not in fragment._request_global:
            _unique_append(fragment.requests_for_write, name)


    def on_declare(self, fragment, name, *, position=None):
        """
        Record a declaration (define, params, bindings) of name, a
        symbol. Removes name from the read and write requests and adds
        to declares. A name the fragment already pulls from the module
        is the local-or-global conflict, except at module scope, where
        a declaration and a global request name the same binding
        """

        if name in fragment._request_global:
            if _in_module_scope(fragment):
                fragment._request_global.remove(name)
            else:
                raise CompilerError(_LOCAL_GLOBAL_CONFLICT % (str(name),), name)

        for bucket in (fragment.requests, fragment.requests_for_write):
            if name in bucket:
                bucket.remove(name)

        _unique_append(fragment.declares, name)


    def on_explicit_global(self, fragment, name, *, position=None):
        """
        Record explicit global (define-global) of name, a symbol.
        Conflicts with local declaration; removes from requests buckets.
        """

        if name in fragment.declares and not _in_module_scope(fragment):
            raise CompilerError(_LOCAL_GLOBAL_CONFLICT % (str(name),), name)

        for bucket in (fragment.requests, fragment.requests_for_write):
            if name in bucket:
                bucket.remove(name)

        _unique_append(fragment._request_global, name)


    def on_request_var(self, fragment, name):
        """
        Record a closure read request of name, a symbol.
        Adds to requests unless already declared.
        """

        if name not in fragment.declares:
            _unique_append(fragment.requests, name)


    def enter_scope(self):
        """
        No-op for Koskari; used by PythonPragma for explicit-globals tracking
        """

        pass


    def exit_scope(self):
        pass


    def closure_names(self, fragment):
        """
        The symbols a fragment closes over: its requests and write
        requests, less its explicit globals, in first-request order
        """

        seen = set(fragment._request_global)
        closure = []
        for n in fragment.requests + fragment.requests_for_write:
            if n not in seen:
                seen.add(n)
                closure.append(n)
        return closure


    def build_namespace_spec(self, fragment) -> NamespaceSpec:
        """
        Build NamespaceSpec from fragment buckets; the names stay atoms
        until lowering spells them
        """

        return NamespaceSpec(
            fragment.name,
            tuple(fragment.declares),
            tuple(self.closure_names(fragment)),
            tuple(fragment._request_global),
        )


    def validate_tree(self, code, ancestor_nspecs, mode, is_root=False):
        """
        Recursively validate that all writes are satisfied.
        Root (module-level) skips validation to allow implicit global.

        A nested Code's enclosing scopes are the fragments that were open
        when it was completed, recorded by register_code_fragment; the
        constant pool says nothing about nesting, since a let's namespace
        spec sits in its enclosing code's pool beside lambdas the let does
        not enclose. ancestor_nspecs is unused and kept for the signature.
        """

        from ..compiler import CompilerMode

        if mode != CompilerMode.FUNCTION:
            return

        nspec = code.namespace_spec
        if nspec is None:
            return

        # Names that satisfy writes: declared or explicitly global in this
        # scope or one that lexically encloses it. Ancestors are read now,
        # not when the Code was built, so a define later in an enclosing
        # body still counts.
        satisfied = set(nspec.local_syms)
        satisfied.update(nspec.global_syms)
        for ancestor in self._code_ancestors.get(id(code), ()):
            satisfied.update(_spellings(ancestor))

        source_frag = self._code_to_fragment.get(id(code))

        if not is_root:
            for name in self.get_request_for_write_syms(code):
                if name not in satisfied:
                    self._raise_unsatisfied_write(source_frag, name)

        # the let and class bodies compiled inline into this function:
        # their writes originate there and are checked against the scope
        # itself and its own chain, which this function is part of
        if source_frag is not None:
            for scope, ancestors in self._inline_scopes.get(id(source_frag), ()):
                scope_satisfied = set(_spellings(scope))
                for ancestor in ancestors:
                    scope_satisfied.update(_spellings(ancestor))
                for name in scope.requests_for_write:
                    if name in scope.propagated_writes:
                        continue
                    if str(name) not in scope_satisfied:
                        self._raise_unsatisfied_write(scope, str(name))

        # Recurse into nested function Codes; the pool is only how they
        # are found, each carries its own recorded ancestry
        for const in (code.constants_pool or ()):
            if isinstance(const, FunctionSpec) and const.code is not None:
                self.validate_tree(const.code, (), mode, is_root=False)


    def _raise_unsatisfied_write(self, fragment, name):
        pos = self.get_write_position(fragment, name)
        raise CompilerError(
            f"Cannot write to {name!r}: not declared in this scope"
            " or any enclosing scope. Use define for a local or"
            " define-global for a global.",
            name,
            position=pos)


    def cpool_merge(self, fragment, inner_nspec, inner_write_syms):
        """
        Merge inner Code's closure and write needs into outer fragment.
        Called during complete() when scanning for nested FunctionSpecs.

        A write copied up this way keeps the closure chain honest but is
        validated where it originated, against that Code's own lexical
        chain; here it is marked propagated so the outer validation skips
        it. A name the outer body also writes itself stays a write of the
        outer's own.
        """

        # the inner Code spells its names; each becomes the symbol under
        # that spelling as it enters the outer fragment
        for name in inner_nspec.closure_syms:
            sym = symbol(name)
            if sym not in fragment.declares:
                _unique_append(fragment.requests, sym)

        for name in (inner_write_syms or ()):
            sym = symbol(name)
            if sym not in fragment.requests_for_write:
                _unique_append(fragment.propagated_writes, sym)
            self.on_write(fragment, sym)


    def get_write_position(self, fragment, name: str) -> Optional[Tuple]:
        """
        Return (line, col) for first write to name in fragment, or None
        """

        if fragment is None:
            return None
        return self._write_positions.get((id(fragment), name))


    def get_request_for_write_syms(self, code) -> Tuple:
        """
        Return names that require write capability for the given Code.
        Compile-time only; tracked by pragma keyed by Code id.
        """

        return self._code_request_for_write.get(id(code), ())


    def register_code_fragment(self, code, fragment, ancestors=()):
        """
        Register source fragment for a Code (position lookup during
        validation) and the fragments lexically enclosing it, outermost
        first, as they stood open when the Code was completed
        """

        self._code_to_fragment[id(code)] = fragment
        self._code_request_for_write[id(code)] = tuple(
            str(n) for n in fragment.requests_for_write
            if n not in fragment.propagated_writes)
        self._code_ancestors[id(code)] = tuple(ancestors)


    def register_scope_fragment(self, fragment, ancestors=()):
        """
        Register an inline scope (a let or class body) that closed with
        ``ancestors`` open around it, outermost first. Its own writes are
        validated with the Code of the innermost enclosing function; a
        scope with no enclosing function is module level, where writes
        may be implicit globals, and is not registered
        """

        for ancestor in reversed(ancestors):
            if ancestor.function_scope:
                self._inline_scopes.setdefault(id(ancestor), []).append(
                    (fragment, tuple(ancestors)))
                return


    def inline_scopes(self, fragment):
        """
        The inline scopes registered under a function fragment, as
        (scope, ancestors) pairs in the order they closed
        """

        return tuple(self._inline_scopes.get(id(fragment), ()))


class SchemePragma(KoskariPragma):
    """
    Scheme-like: unsatisfied writes promote to global.
    No compile-time error on undeclared writes; runtime resolves from module.
    """

    def validate_tree(self, code, ancestor_nspecs, mode, is_root=False):
        """
        Scheme-like: allow unsatisfied writes; runtime promotes to global.
        """

        from ..compiler import CompilerMode

        if mode != CompilerMode.FUNCTION:
            return

        nspec = code.namespace_spec
        if nspec is None:
            return

        # No strict validation: unsatisfied writes resolve as implicit global
        # at runtime via ns_init_syms fallback to mdl_ns_borrow_ref

        # Recurse into nested function Codes
        for const in (code.constants_pool or ()):
            if isinstance(const, FunctionSpec) and const.code is not None:
                self.validate_tree(const.code, (), mode, is_root=False)


class PythonPragma(KoskariPragma):
    """
    Python-like: implicit local on first write unless declared global.
    Tracks explicit-globals per scope for nested scopes.
    """

    def __init__(self):
        super().__init__()
        # Stack of sets of names declared global in current scope hierarchy
        self._explicit_globals_stack = []


    def enter_scope(self):
        """
        Push new scope for explicit-globals tracking
        """

        self._explicit_globals_stack.append(set())


    def exit_scope(self):
        """
        Pop scope from explicit-globals stack
        """

        self._explicit_globals_stack.pop()


    def on_write(self, fragment, name, *, position=None):
        """
        Bind a write where the open scopes put it. A scope that already
        declares the name, a let included, takes the write as its own
        binding; one that declared it global makes it a global write.
        A name no open scope declares is an implicit declaration in the
        nearest function or class body, the only scopes Python has to
        declare in, so a first write inside a let is a local of the
        function, visible after the let, not a let-local.
        """

        home = None
        for scope in self._open_scopes(fragment):
            if name in scope.declares:
                super().on_write(fragment, name, position=position)
                return
            if name in scope._request_global:
                self.on_explicit_global(fragment, name, position=position)
                return
            if scope.function_scope or scope.collector_scope:
                home = scope
                break

        if self._explicit_globals_stack \
           and name in self._explicit_globals_stack[-1]:
            self.on_explicit_global(fragment, name, position=position)
        elif home is None or home is fragment:
            self.on_declare(fragment, name, position=position)
        else:
            home.declare_var(name)
            super().on_write(fragment, name, position=position)


    @staticmethod
    def _open_scopes(fragment):
        """
        The scopes a write in fragment can see, innermost first: the
        fragment itself, then the compiler's open lexical chain
        """

        yield fragment
        compiler = fragment.compiler
        if compiler is not None:
            for scope in reversed(compiler.lexical_stack):
                if scope is not fragment:
                    yield scope


    def on_explicit_global(self, fragment, name, *, position=None):
        """
        Record name as global for current scope, then delegate to base
        """

        if self._explicit_globals_stack:
            self._explicit_globals_stack[-1].add(name)
        super().on_explicit_global(fragment, name, position=position)


# The end.
