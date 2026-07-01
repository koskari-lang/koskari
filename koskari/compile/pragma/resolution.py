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


from enum import Enum
from typing import Optional, Protocol, Tuple

from ...exceptions import CompilerError
from ...lib import FunctionSpec, NamespaceSpec


class ResolutionMode(Enum):
    """
    Resolution modes: Koskari, Scheme, Python
    """

    KOSKARI = "koskari"
    SCHEME = "scheme"
    PYTHON = "python"


def _unique_append(lst, val):
    """
    Append val to lst only if not already present
    """

    if val not in lst:
        lst.append(val)


class ResolutionPragma(Protocol):
    """
    Protocol for resolution semantics during compilation
    """

    def on_write(self, fragment, name: str, *, position=None):
        """
        Invoked when a write (setq, delq, inplace) is encountered
        """
        ...


    def on_declare(self, fragment, name: str, *, position=None):
        """
        Invoked when a name is declared (define, params, bindings)
        """
        ...


    def on_explicit_global(self, fragment, name: str, *, position=None):
        """
        Invoked when a name is explicitly declared global (define-global)
        """
        ...


    def on_request_var(self, fragment, name: str):
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


    def build_namespace_spec(self, fragment) -> NamespaceSpec:
        """
        Build NamespaceSpec from fragment buckets
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


    def on_write(self, fragment, name: str, *, position=None):
        """
        Record a write (setq, delq, inplace) to name.
        Adds to requests_for_write unless already declared or global.
        """

        name = str(name)

        # Store first occurrence position for validation error messages
        if position is not None:
            key = (id(fragment), name)
            if key not in self._write_positions:
                self._write_positions[key] = position

        if name not in fragment.declares and name not in fragment._request_global:
            _unique_append(fragment.requests_for_write, name)


    def on_declare(self, fragment, name: str, *, position=None):
        """
        Record a declaration (define, params, bindings).
        Removes name from other buckets and adds to declares.
        """

        name = str(name)

        for bucket in (fragment.requests, fragment.requests_for_write,
                       fragment._request_global):
            if name in bucket:
                bucket.remove(name)

        _unique_append(fragment.declares, name)


    def on_explicit_global(self, fragment, name: str, *, position=None):
        """
        Record explicit global (define-global).
        Conflicts with local declaration; removes from requests buckets.
        """

        name = str(name)
        if name in fragment.declares:
            raise CompilerError(
                '%r cannot be both local (define) and global (define-global).'
                % (name,),
                name)

        for bucket in (fragment.requests, fragment.requests_for_write):
            if name in bucket:
                bucket.remove(name)

        _unique_append(fragment._request_global, name)


    def on_request_var(self, fragment, name: str):
        """
        Record a closure read request.
        Adds to requests unless already declared.
        """

        name = str(name)
        if name not in fragment.declares:
            _unique_append(fragment.requests, name)


    def enter_scope(self):
        """
        No-op for Koskari; used by PythonPragma for explicit-globals tracking
        """

        pass


    def exit_scope(self):
        pass


    def build_namespace_spec(self, fragment) -> NamespaceSpec:
        """
        Build NamespaceSpec from fragment buckets.
        Closure = requests + requests_for_write minus globals.
        """

        seen = set(fragment._request_global)
        closure = []
        for n in fragment.requests + fragment.requests_for_write:
            if n not in seen:
                seen.add(n)
                closure.append(n)

        global_syms = tuple(fragment._request_global)
        return NamespaceSpec(
            fragment.name,
            tuple(fragment.declares),
            tuple(closure),
            global_syms,
        )


    def validate_tree(self, code, ancestor_nspecs, mode, is_root=False):
        """
        Recursively validate that all writes are satisfied.
        Root (module-level) skips validation to allow implicit global.
        """

        from ..compiler import CompilerMode

        if mode != CompilerMode.FUNCTION:
            return

        nspec = code.namespace_spec
        if nspec is None:
            return

        # Names that satisfy writes: local or global in this or ancestor scopes
        accum = []
        accum.extend(nspec.local_syms)
        accum.extend(nspec.global_syms)
        for ans in ancestor_nspecs:
            accum.extend(ans.local_syms)
            accum.extend(ans.global_syms)
        satisfied = set(accum)

        if not is_root:
            request_for_write_syms = self.get_request_for_write_syms(code)
            for name in request_for_write_syms:
                if name not in satisfied:
                    source_frag = self._code_to_fragment.get(id(code))
                    pos = self.get_write_position(source_frag, name)
                    raise CompilerError(
                        'Cannot write to %r: not declared in this scope or'
                        ' any enclosing scope. Use define for a local or'
                        ' define-global for a global.' % (name,),
                        name,
                        position=pos)

        # Recurse into nested function Codes
        cpool_nspecs = tuple(
            c for c in (code.constants_pool or ())
            if isinstance(c, NamespaceSpec))
        next_ancestors = ancestor_nspecs + (nspec,) + cpool_nspecs

        for const in (code.constants_pool or ()):
            if isinstance(const, FunctionSpec) and const.code is not None:
                self.validate_tree(
                    const.code, next_ancestors, mode, is_root=False)


    def cpool_merge(self, fragment, inner_nspec, inner_write_syms):
        """
        Merge inner Code's closure and write needs into outer fragment.
        Called during complete() when scanning for nested FunctionSpecs.
        """

        closure_syms = inner_nspec.closure_syms
        for name in closure_syms:
            if name not in fragment.declares:
                _unique_append(fragment.requests, name)

        for name in (inner_write_syms or ()):
            self.on_write(fragment, name)


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


    def register_code_fragment(self, code, fragment):
        """
        Register source fragment for a Code (position lookup during validation)
        """

        self._code_to_fragment[id(code)] = fragment
        self._code_request_for_write[id(code)] = tuple(fragment.requests_for_write)


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
        cpool_nspecs = tuple(
            c for c in (code.constants_pool or ())
            if isinstance(c, NamespaceSpec))
        next_ancestors = ancestor_nspecs + (nspec,) + cpool_nspecs

        for const in (code.constants_pool or ()):
            if isinstance(const, FunctionSpec) and const.code is not None:
                self.validate_tree(
                    const.code, next_ancestors, mode, is_root=False)


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


    def on_write(self, fragment, name: str, *, position=None):
        """
        If name is in explicit-globals for current scope, treat as global.
        Otherwise implicit local: declare on first write (Scheme-like).
        """

        name = str(name)
        if self._explicit_globals_stack \
           and name in self._explicit_globals_stack[-1]:
            self.on_explicit_global(fragment, name, position=position)
        else:
            self.on_declare(fragment, name, position=position)


    def on_explicit_global(self, fragment, name: str, *, position=None):
        """
        Record name as global for current scope, then delegate to base
        """

        if self._explicit_globals_stack:
            self._explicit_globals_stack[-1].add(str(name))
        super().on_explicit_global(fragment, name, position=position)


# The end.
