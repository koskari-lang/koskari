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


import struct
import warnings
import weakref
from abc import ABC, abstractmethod
from contextlib import contextmanager
from enum import IntEnum, auto

from ..exceptions import CompilerError, UncaughtCompilerError
from ..lib import (Arena, Datum, Code, FormalsSpec,
                   FunctionSpec, Module, ModuleNamespace,
                   Namespace, NamespaceSpec, OpCode,
                   bracket_nil, bracket_pair, cons, keyword,
                   kos_repr, nil, symbol)
from ..types import (fill_position, is_nil, get_position, is_keyword,
                     is_pair, is_proper, is_symbol, parameterize)
from .pragma import (BracketCalls, KeywordLiterals, KoskariPragma,
                     PackModule, ResolutionMode, StrayColon)
from .processor import ByteCodeProcessor


class Compiled(ABC):

    @abstractmethod
    def compile_impl(self, compiler, source_obj, tc):
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


def unique_append(lst, val):
    """
    Append val to lst only if it is not already present

    :param lst: a mutable sequence
    :param val: value to append
    """

    if val not in lst:
        lst.append(val)


_PTR_SIZE = struct.calcsize("P")


def _arena_member_size(code):
    """
    The bytes a code object's unified block occupies inside an arena:
    the unrounded block total, aligned up to pointer size to match the
    arena's reservation alignment.

    :param code: a Code object
    """

    size = code.payload_bytes + code.padding_bytes
    return ((size + _PTR_SIZE - 1) // _PTR_SIZE) * _PTR_SIZE


class ByteCodeFragment:
    """
    Holds a fragment of bytecode during compilation: instructions plus
    closure requests and name declarations for the namespace
    """

    def __init__(self, compiler=None, name=None, filename=None, mode=None,
                 resolution=None):
        """
        :param compiler: the compiler/builder used to complete this fragment
        :param name: optional fragment name (e.g. for a block or loop)
        :param filename: optional source filename
        :param mode: optional compilation mode
        :param resolution: resolution instance; default KoskariPragma when omitted
        """

        self.name = name
        self.filename = filename
        self.mode = mode
        self.compiler = compiler
        self.resolution = resolution or KoskariPragma()
        self.instructions = []
        self.requests = []
        self.requests_for_write = []
        self._request_global = []
        self.declares = []
        self.annotation_bindings = []
        self.doc = None


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

        self.resolution.on_request_var(self, str(var_name))


    def request_for_write(self, var_name, position=None):
        """
        Record that var_name must be writable (setq, delq, inplace).

        :param var_name: name that must be writable
        :param position: optional (line, col) for error reporting
        """

        self.resolution.on_write(self, str(var_name), position=position)


    def request_global(self, var_name, position=None):
        """
        Record explicit need from module (define-global).

        :param var_name: name to pull from module
        :param position: optional (line, col) for error reporting
        """

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
        with compiler.resolution_scope():
            ns_name = str(ns_name)
            child = compiler.fragment(ns_name)

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
                    valfrag.merge(compiler.full_compile(val, False))

            yield child
            nspec = child.as_namespace_spec()

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
        with compiler.resolution_scope():
            ns_name = str(ns_name)
            child = compiler.fragment(ns_name)

            yield child
            nspec = child.as_namespace_spec()

            self.instruction(OpCode.PUSH_COLLECT, nspec)
            self.instructions.extend(child.instructions)
            self.instruction(OpCode.POP_COLLECT, 0)


    def as_bytecode(self):
        if self.compiler is None:
            raise TypeError("ByteCodeFragment has no compiler")
        return self.compiler.complete(self, is_root=True)


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


    def error(self, msg, source):
        raise CompilerError(msg, source)


    @contextmanager
    def resolution_scope(self):
        reso = self._resolution_instance
        if reso is not None:
            reso.enter_scope()
        self.scope_depth += 1
        try:
            yield
        finally:
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

        ns = vars(self.module)
        v = ns.get(name)
        if v and isinstance(v, Compiled):
            return v
        return None


    def find_compiled(self, name):
        """
        Resolve compile/<name> in the module namespace

        :param name: symbol or string for the compile-time object (e.g. if, begin)
        :return: Compiled instance, or None
        """

        name = str(name)

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


    def fragment(self, name=None, mode=None):
        reso = self._resolution_instance
        if reso is None:
            reso = self._resolution_pragma_class()
        return ByteCodeFragment(
            self, str(name), self.filename, mode=mode, resolution=reso)


    def compile(self, source_obj, tc=False, position=None):
        if isinstance(source_obj, ByteCodeFragment):
            return source_obj

        tmp = self._resolution_instance is None
        if tmp:
            self._resolution_instance = self._resolution_pragma_class()

        try:
            fragment = self.fragment(self.name)
            ret = self.compile_expr(fragment, source_obj, tc, position)
            # None is the emitted-into-fragment sentinel; a falsy
            # form (an expansion to False, 0, "") is a real result
            return fragment if ret is None else ret
        finally:
            if tmp:
                self._resolution_instance = None


    def full_compile(self, source_obj, tc=False, position=None):
        tmp = self._resolution_instance is None
        if tmp:
            self._resolution_instance = self._resolution_pragma_class()
        try:
            res = source_obj
            while not isinstance(res, ByteCodeFragment):
                res = self.compile(res, tc, position)
            return res
        finally:
            if tmp:
                self._resolution_instance = None


    def compile_expression(self, source_obj, tc=False):
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
        :param tc: tail-call flag
        :return: compiled ByteCodeFragment
        """

        with parameterize(**{ACTIVE_PRAGMA_PARAM: self.pragma}):
            return self.full_compile(source_obj, tc)


    def compile_expr(self, fragment, source_obj, tc, position=None):
        """
        ``position`` is the innermost enclosing source position known to
        the caller, threaded to diagnostics about positionless atoms; a
        source object carrying its own position wins over it
        """

        # tail-call promotion moved into the processor's
        # promote_tail_calls pass; the specials' tc threading is pinned
        # off here pending removal of the parameter itself
        tc = False

        if is_symbol(source_obj):
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
            ret = dispatch(fragment, source_obj, tc, position)
            # None is the emitted-into-fragment sentinel; a falsy form
            # (an expansion to False, 0, "") is a real result
            return fragment if ret is None else ret
        except CompilerError:
            raise
        except Exception as ex:
            from ..exceptions import KoskariSyntaxError
            if isinstance(ex, KoskariSyntaxError):
                raise
            raise UncaughtCompilerError(ex, source_obj) from ex


    def compile_const(self, fragment, value, tc, position=None):
        fragment.instruction(OpCode.CONST, value)
        return fragment


    def compile_symbol(self, fragment, sym, tc, position=None):

        if sym is _symbol_colon:
            self.check_stray_colon(sym)

        # TODO: we may want to add an alias/FOO form for
        # compile-time symbolic aliases. These would be distinct
        # from compile/FOO forms which compile over their full
        # source list -- the aliases would only expand the symbol
        # itself.

        if sym in _COMPILER_LITERALS:
            return self.compile_const(fragment, _COMPILER_LITERALS[sym], tc)

        comp = self.find_bare_compiled(sym)
        if comp is not None:
            # the atom is interned and positionless; hand the bare arm
            # the caller-threaded position via the Datum carrier
            return self.compile_special(
                fragment, comp, Datum(sym, position), tc)

        # This is handled by the reader macros now.
        # ex = sym.rsplit(".", 1)
        # if len(ex) > 1:
        #     source = cons(_symbol_attr, *ex, nil)
        #     return self.compile_pair(fragment, source, tc)

        return self.compile_raw_symbol(fragment, sym, tc)


    def compile_raw_symbol(self, fragment, sym, tc):
        strsym = str(sym)
        fragment.request_var(strsym)
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


    def compile_pair(self, fragment, source_obj, tc, position=None):
        # print("compile_pair", kos_repr(source_obj))

        if is_nil(source_obj):
            return self.compile_const(fragment, source_obj, False)

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
                return self.compile_special(fragment, comp, source_obj, tc)

            if head is _symbol_call:
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

        if not is_symbol(head):
            return None

        comp = self.find_compiled(head)
        if comp is None:
            return None

        res = comp.compile_impl(self, form, False)
        if res is None or isinstance(res, ByteCodeFragment):
            return None
        return res


    def compile_special(self, fragment, comp, source_obj, tc):
        res = comp.compile_impl(self, source_obj, tc)
        if isinstance(res, ByteCodeFragment):
            fragment.merge(res)
            return fragment
        return res


    def _cpool_scan_merge(self, fragment, instr):
        """
        Scan instructions for CONST (FunctionSpec) and merge inner Code's
        closure_syms and request_for_write_syms into the fragment.
        """

        reso = fragment.resolution
        for op, arg in instr:
            if op == OpCode.CONST and isinstance(arg, FunctionSpec):
                code = arg.code
                if code is None:
                    continue
                nspec = code.namespace_spec
                if nspec is None:
                    continue
                request_for_write_syms = reso.get_request_for_write_syms(code)
                reso.cpool_merge(fragment, nspec, request_for_write_syms)


    def complete(self, fragment, mode=None, _validate=True, is_root=False):
        """
        Complete a fragment into a Code object.

        :param fragment: the fragment to complete
        :param mode: the mode to complete in
        :param _validate: whether to validate the code
        :param is_root: whether this is the root fragment
        :return: the completed Code object
        """

        if mode is None:
            mode = self.mode

        reso = fragment.resolution
        base_nspec = None

        def resolve_nspec(rows):
            # Runs between the compress/normalize passes and the fast
            # lowering: merge closure/write syms from inner FunctionSpecs
            # into the fragment, then build the NamespaceSpec from the
            # fragment buckets (local, closure, global).
            nonlocal base_nspec
            self._cpool_scan_merge(fragment, rows)
            base_nspec = reso.build_namespace_spec(fragment)
            return base_nspec

        # One use-and-discard processor per stream runs the gated
        # transformation pipeline (compress, operand normalization, the
        # optional fast lowering, generator tail-call demotion).
        proc = ByteCodeProcessor(
            fragment.instructions,
            gates={'convert-fast': self.convert_fast_enabled,
                   'tco': self.tco_enabled},
            mode=mode)
        instr = proc.process(resolve_nspec)

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
        reso.register_code_fragment(code, fragment)

        # Recursively validate that all writes are satisfied.
        # Only validate from root complete(); inner complete() calls skip so
        # nested functions get validated with proper ancestor context.
        if _validate and is_root:
            reso.validate_tree(
                code, (), CompilerMode.FUNCTION, is_root=True)

        # finally, register the code with the minted registry so that it
        # can potentially be sealed later.
        self._minted.append(weakref.ref(code))

        return code


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
