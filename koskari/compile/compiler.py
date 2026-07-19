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
from ..lib import (Arena, Compare, Operand, Code, FormalsSpec, FunctionSpec,
                   Module, ModuleNamespace, NAMESPACE_FAST_SLOT_MAX,
                   Namespace, NamespaceSpec, OpCode, OpStyle, bracket_nil,
                   bracket_pair, cons, keyword, nil, symbol)
from ..types import is_nil, get_position, is_pair, is_proper, is_symbol
from .pragma import BracketCalls, KoskariPragma, PackModule, ResolutionMode


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


def _build_var_to_fast():
    # Pre-compiled map: VAR opcode -> FAST opcode for convert_fast lowering.
    # Built by finding each FAST_* opcode and resolving to a matching VAR_*
    # opcode.

    var_to_fast = {}
    for opcode in OpCode:
        opname = opcode.name
        if not (opname.startswith('KOS_OP_FAST')
                or opname.startswith('KOS_OP_CALL_FAST')):
            continue
        varname = opname.replace('FAST', 'VAR', 1)
        if hasattr(OpCode, varname):
            var_to_fast[OpCode[varname]] = opcode

    var_to_fast[OpCode.KOS_OP_UNPACK_VAR] = OpCode.KOS_OP_UNPACK_FAST
    var_to_fast[OpCode.KOS_OP_SWITCH_EQ_VAR] = OpCode.KOS_OP_SWITCH_EQ_FAST
    var_to_fast[OpCode.KOS_OP_SWITCH_EQ_VAR_ATTR] = OpCode.KOS_OP_SWITCH_EQ_FAST_ATTR
    var_to_fast[OpCode.KOS_OP_OBJECT_COMPARE_VAR] = OpCode.KOS_OP_OBJECT_COMPARE_FAST

    return var_to_fast


VAR_TO_FAST = _build_var_to_fast()


_OPERAND_COUNT = len(Operand)


def merge_opcount(value):
    """
    Normalize a KOSTYLE_OPCOUNT operand to its merged uint16 (operator selector
    in the high byte, reduce count in the low byte).

    Accepts either idiom, so specials may emit whichever reads cleanly:
      - a ``(op_type, count)`` pair, which is merged, or
      - a pre-merged int, which is validated to have been correctly packed.

    Both are range- and op-type-checked, so no soften-side reversal is needed.

    :param value: (op_type, count) pair, or a pre-merged int
    :return: merged uint16 int
    :raises ValueError: on a bad shape, out-of-range byte, or unknown op-type
    """

    if isinstance(value, tuple):
        if len(value) != 2:
            raise ValueError("opcount operand must be (op_type, count), "
                             "got %r" % (value,))
        op_type, count = value
        op_type = int(getattr(op_type, 'value', op_type))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError("opcount int out of uint16 range: %r" % (value,))
        op_type, count = merged >> 8, merged & 0xff

    if not 0 <= op_type < _OPERAND_COUNT:
        raise ValueError("opcount op-type is not a known Operand: %r" % (op_type,))
    if not 0 <= count <= 0xff:
        raise ValueError("opcount count out of byte range: %r" % (count,))

    return (op_type << 8) | count


_COMPARE_COUNT = len(Compare)


def merge_cmpcount(value):
    """
    Normalize a KOSTYLE_CMPCOUNT operand to its merged uint16 (Compare
    selector in the high byte, reduce count in the low byte). The
    CMPCOUNT twin of merge_opcount: accepts a (selector, count) pair or
    a pre-merged int, both range- and selector-checked.

    :param value: (selector, count) pair, or a pre-merged int
    :return: merged uint16 int
    :raises ValueError: on a bad shape, out-of-range byte, or unknown selector
    """

    if isinstance(value, tuple):
        if len(value) != 2:
            raise ValueError("cmpcount operand must be (selector, count), "
                             "got %r" % (value,))
        sel, count = value
        sel = int(getattr(sel, 'value', sel))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError("cmpcount int out of uint16 range: %r" % (value,))
        sel, count = merged >> 8, merged & 0xff

    if not 0 <= sel < _COMPARE_COUNT:
        raise ValueError("cmpcount selector is not a known Compare: %r" % (sel,))
    if not 0 <= count <= 0xff:
        raise ValueError("cmpcount count out of byte range: %r" % (count,))

    return (sel << 8) | count


def split_cmpcount(value):
    """
    Split a CMPCOUNT operand -- (selector, count) pair or merged int --
    into a validated (selector, count) int pair.
    """

    merged = merge_cmpcount(value)
    return (merged >> 8, merged & 0xff)


# CMPSRC packing (mirrors the KosCmpSrc_* macros in bytecode.h):
# selector in bits 0-3, per-operand constant-source bits above.
CMPSRC_X_CONST = 0x10
CMPSRC_Y_CONST = 0x20


def make_cmpsrc(sel, x_const, y_const):
    """
    Pack a CMPSRC field from a Compare selector and two source flags
    """

    return ((int(getattr(sel, 'value', sel)) & 0x0f)
            | (CMPSRC_X_CONST if x_const else 0)
            | (CMPSRC_Y_CONST if y_const else 0))


_TAIL_CALL_TO_CALL = {
    OpCode.KOS_OP_TAIL_CALL: OpCode.KOS_OP_CALL,
    OpCode.KOS_OP_TAIL_CALL_NAMES: OpCode.KOS_OP_CALL_NAMES,
    OpCode.KOS_OP_TAIL_CALL_STAR: OpCode.KOS_OP_CALL_STAR,
}


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
_symbol_attr = symbol("compile/attr")
_symbol_Ellipsis = symbol("Ellipsis")
_symbol_ellipsis = symbol("...")
_symbol_nil = symbol("nil")

_symbol_call = symbol("compile/call")
_symbol_cons = symbol("compile/cons")


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
            self.instruction(OpCode.KOS_OP_POSITION, (line, col, extra))


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
                OpCode.KOS_OP_PUSH_NS,
                (nspec, tuple(names) if names else None, 0, 0))
            self.instructions.extend(child.instructions)
            self.instruction(OpCode.KOS_OP_POP_NS, 0)


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

            self.instruction(OpCode.KOS_OP_PUSH_COLLECT, nspec)
            self.instructions.extend(child.instructions)
            self.instruction(OpCode.KOS_OP_POP_COLLECT, 0)


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


_CHECK_SQUASHES = {
    OpCode.KOS_OP_OBJECT_NOT: OpCode.KOS_OP_JUMP_TRUE,
    OpCode.KOS_OP_EXCEPTION_MATCH: OpCode.KOS_OP_CHECK_EXCEPTION,
    OpCode.KOS_OP_NILP: OpCode.KOS_OP_CHECK_NIL,
}


_SWITCH_SQUASHES = {
    OpCode.KOS_OP_CONST: OpCode.KOS_OP_SWITCH_EQ_CONST,
    OpCode.KOS_OP_VAR: OpCode.KOS_OP_SWITCH_EQ_VAR,
    OpCode.KOS_OP_VAR_ATTR: OpCode.KOS_OP_SWITCH_EQ_VAR_ATTR,
}


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
            bracket_calls=BracketCalls.IGNORE):

        self.name = name

        # settings, possibly from pragma
        self.convert_fast_enabled = True
        self.tco_enabled = tco_enabled
        self.resolution_mode = resolution_mode
        self._resolution_instance = None
        self.bracket_calls = bracket_calls
        self.protect_code = False
        self.pack_module = PackModule(True, 0, 0)
        self.cache_expression = False
        self.cache_function = True

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

        self.convert_fast_enabled = pragma.get("compiler/convert-fast")
        self.tco_enabled = pragma.get("compiler/tco-enable")

        self.resolution_mode = pragma.get("compiler/resolution")
        self._resolution_instance = None

        self._bracket_calls = pragma.get("compiler/call-brackets")

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

        found = self._find_compiled(name)
        if found is None:
            if not name.startswith("compile/"):
                return self._find_compiled("compile/" + name)
        return found


    def fragment(self, name=None, mode=None):
        reso = self._resolution_instance
        if reso is None:
            reso = self._resolution_pragma_class()
        return ByteCodeFragment(
            self, str(name), self.filename, mode=mode, resolution=reso)


    def compile(self, source_obj, tc=False):
        if isinstance(source_obj, ByteCodeFragment):
            return source_obj

        tmp = self._resolution_instance is None
        if tmp:
            self._resolution_instance = self._resolution_pragma_class()

        try:
            fragment = self.fragment(self.name)
            return self.compile_expr(fragment, source_obj, tc) or fragment
        finally:
            if tmp:
                self._resolution_instance = None


    def full_compile(self, source_obj, tc=False):
        tmp = self._resolution_instance is None
        if tmp:
            self._resolution_instance = self._resolution_pragma_class()
        try:
            res = source_obj
            while not isinstance(res, ByteCodeFragment):
                res = self.compile(res, tc)
            return res
        finally:
            if tmp:
                self._resolution_instance = None


    def compile_expr(self, fragment, source_obj, tc):
        tc = self.tco_enabled and tc

        if is_symbol(source_obj):
            dispatch = self.compile_symbol
        elif is_pair(source_obj):
            dispatch = self.compile_pair
        elif _is_const(source_obj):
            dispatch = self.compile_const
        else:
            raise CompilerError("Unsupported source object", source_obj)

        try:
            return dispatch(fragment, source_obj, tc) or fragment
        except CompilerError:
            raise
        except Exception as ex:
            from ..exceptions import KoskariSyntaxError
            if isinstance(ex, KoskariSyntaxError):
                raise
            raise UncaughtCompilerError(ex, source_obj) from ex


    def compile_const(self, fragment, value, tc):
        fragment.instruction(OpCode.KOS_OP_CONST, value)
        return fragment


    def compile_symbol(self, fragment, sym, tc):

        # TODO: we may want to add an alias/FOO form for
        # compile-time symbolic aliases. These would be distinct
        # from compile/FOO forms which compile over their full
        # source list -- the aliases would only expand the symbol
        # itself.

        if sym in _COMPILER_LITERALS:
            return self.compile_const(fragment, _COMPILER_LITERALS[sym], tc)

        # comp = self.find_compiled(sym)
        # if comp and _is_alias(comp):
        #     return self.compile_special(fragment, comp, sym, tc)

        # This is handled by the reader macros now.
        # ex = sym.rsplit(".", 1)
        # if len(ex) > 1:
        #     source = cons(_symbol_attr, *ex, nil)
        #     return self.compile_pair(fragment, source, tc)

        return self.compile_raw_symbol(fragment, sym, tc)


    def compile_raw_symbol(self, fragment, sym, tc):
        strsym = str(sym)
        fragment.request_var(strsym)
        fragment.instruction(OpCode.KOS_OP_VAR, strsym)
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

        # WARN: attribute the diagnostic to the koskari source location, not to
        # this compiler frame, so the output isn't compiler-internal noise
        # (no compiler.py path, no echoed warnings.warn() line).
        pos = get_position(source_obj)
        lineno = pos[0] if pos else 0
        warnings.warn_explicit(msg, SyntaxWarning,
                               self.filename or "<unknown>", lineno)


    def compile_pair(self, fragment, source_obj, tc):
        # print("compile_pair", kos_repr(source_obj))

        if is_nil(source_obj):
            return self.compile_const(fragment, source_obj, False)

        self._check_bracket_call(source_obj)

        fragment.position_of(source_obj)

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

        # Re-emit as an explicit call form with the head left as raw
        # source. Compiling the head here would destroy forms that
        # compile/call's patterns need to see (eg. a (compile/attr obj
        # name) head routing to compile/call-attr); the special compiles
        # the callable itself when no pattern claims it.
        return cons(_symbol_call, head, tail)


    def compile_special(self, fragment, comp, source_obj, tc):
        res = comp.compile_impl(self, source_obj, tc)
        if isinstance(res, ByteCodeFragment):
            fragment.merge(res)
            return fragment
        return res


    def compress(self, instr):
        squished = [(OpCode.KOS_OP_NOOP, 0)]

        for (op, arg) in instr:
            if ((op == OpCode.KOS_OP_POP
                 and arg == 1
                 and squished[-1][0] in (OpCode.KOS_OP_CONST,
                                         OpCode.KOS_OP_DUP))):
                del squished[-1]
                continue

            elif (op == OpCode.KOS_OP_OBJECT_NOT):
                prior = squished[-1][0]
                if prior == OpCode.KOS_OP_CONST:
                    val = squished[-1][1]
                    squished[-1] = (OpCode.KOS_OP_CONST, not val)
                    continue

            elif (op == OpCode.KOS_OP_JUMP_TRUE
                  and squished[-1][0] == OpCode.KOS_OP_CONST):
                truthy = bool(squished[-1][1])
                del squished[-1]
                if truthy:
                    op = OpCode.KOS_OP_JUMP
                else:
                    continue

            elif (op == OpCode.KOS_OP_JUMP_FALSE):
                prior, prior_arg = squished[-1]

                if prior == OpCode.KOS_OP_CONST:
                    # jumping based on a constant value
                    truthy = not squished[-1][1]
                    del squished[-1]
                    if truthy:
                        op = OpCode.KOS_OP_JUMP
                    else:
                        continue

                elif prior in _CHECK_SQUASHES:
                    # here we're combining an object operand with a jump
                    # into a single check jump
                    del squished[-1]
                    op = _CHECK_SQUASHES[prior]

                elif prior == OpCode.KOS_OP_OBJECT_COMPARE:
                    # fuse the compare with its jump: CHECK_COMPARE
                    # branches on chain failure, carrying the selector,
                    # count, and target in one EXTRA row
                    del squished[-1]
                    sel, cnt = split_cmpcount(prior_arg)
                    op = OpCode.KOS_OP_CHECK_COMPARE
                    arg = (sel, cnt, arg, None)

            elif op == OpCode.KOS_OP_SWITCH_EQ:
                prior = squished[-1]
                if prior[0] in _SWITCH_SQUASHES:
                    op = _SWITCH_SQUASHES[prior[0]]
                    pargs = prior[1]
                    if isinstance(pargs, tuple):
                        arg = (arg, *pargs[:3])
                    else:
                        arg = (arg, pargs, None, None)
                    del squished[-1]

            elif op == OpCode.KOS_OP_LABEL:
                # omit useless jump when the jump target is the next instruction
                # JUMP foo, LABEL foo -> NOOP
                prior = squished[-1]
                if prior[0] == OpCode.KOS_OP_JUMP and prior[1] == arg:
                    del squished[-1]

                elif (prior[0] in (OpCode.KOS_OP_JUMP_TRUE,
                                   OpCode.KOS_OP_JUMP_FALSE)
                      and prior[1] == arg):
                    if len(squished) > 1 and squished[-2][0] == OpCode.KOS_OP_CONST:
                        del squished[-1]
                        del squished[-1]
                    else:
                        squished[-1] = (OpCode.KOS_OP_POP, 1)

            elif ((op in (OpCode.KOS_OP_RETURN,
                          OpCode.KOS_OP_BREAK,
                          OpCode.KOS_OP_CONTINUE,
                          OpCode.KOS_OP_YIELD)
                   and arg == 1
                   and squished[-1][0] == OpCode.KOS_OP_CONST
                   and squished[-1][1] is None)):
                # CONST None, RETURN 1 -> RETURN 0
                del squished[-1]
                arg = 0

            elif (op in (OpCode.KOS_OP_CAR, OpCode.KOS_OP_CDR)
                  and squished[-1][0] == op):
                prev_op, prev_arg = squished[-1]
                squished[-1] = (prev_op, prev_arg + arg)
                continue

            elif op == OpCode.KOS_OP_VAR_STORE:
                prior = squished[-1]
                if prior[0] == OpCode.KOS_OP_CONST:
                    del squished[-1]
                    op = OpCode.KOS_OP_VAR_STORE_CONST
                    arg = (arg, prior[1], None, None)

            elif op == OpCode.KOS_OP_VAR_OBJECT_INPLACE:
                # CONST rhs on a single-operand augmented assignment
                # fuses to the stack-free form: bare operator selector,
                # name, pooled operand
                prior = squished[-1]
                if prior[0] == OpCode.KOS_OP_CONST:
                    merged = merge_opcount(arg[0])
                    if merged & 0xff == 1:
                        del squished[-1]
                        op = OpCode.KOS_OP_VAR_OBJECT_INPLACE_CONST
                        arg = (merged >> 8, arg[1], prior[1], None)

            squished.append((op, arg))

        if squished[-1][0] != OpCode.KOS_OP_RETURN:
            if squished[-1][0] == OpCode.KOS_OP_CONST \
               and squished[-1][1] is None:
                squished[-1] = (OpCode.KOS_OP_RETURN, 0)
            else:
                squished.append((OpCode.KOS_OP_RETURN, 1))

        if len(squished) > 1:
            # if we actually had ops, we'll have a leading NOOP that we can
            # remove
            squished = squished[1:]

        return self.pack_var_runs(self.fuse_check_feeders(squished))


    def fuse_check_feeders(self, instr):
        """
        Late sweep: absorb the operand feeders of comparison rows. Two
        patterns, both requiring the immediately preceding value rows to
        be bare VAR or CONST loads (POSITION rows are transparent -- the
        fused row keeps the position in effect at the first feeder, and
        the latest interior position is re-emitted after the row):

        - arity-2 CHECK_COMPARE (not both feeders CONST -- that is a
          folding case): the trio collapses into CHECK_COMPARE_VAR with
          a CMPSRC field carrying the selector and per-operand source
          bits.
        - the ternary bounds shape CONST / VAR / CONST feeding an
          arity-3 comparison: collapses into the bool-producing
          OBJECT_COMPARE_VAR. In branch position (an arity-3
          CHECK_COMPARE) the row cannot also carry the label -- five
          fields -- so the check splits back into OBJECT_COMPARE_VAR
          plus JUMP_FALSE.

        The fast pass later lowers both VAR forms to their FAST twins.
        Runs before pack_var_runs so fusion gets first claim on the
        feeders.
        """

        result = []
        feeders = (OpCode.KOS_OP_VAR, OpCode.KOS_OP_CONST)

        def scan_back(want):
            # feeder indices (nearest first) and interior POSITION rows
            found = []
            pos_rows = []
            i = len(result) - 1
            while i >= 0 and len(found) < want:
                rop = result[i][0]
                if rop == OpCode.KOS_OP_POSITION:
                    pos_rows.append(i)
                elif rop in feeders:
                    found.append(i)
                else:
                    break
                i -= 1
            return (found if len(found) == want else None), pos_rows

        def splice(start, pos_rows, rows):
            trailing = result[max(pos_rows)] if pos_rows else None
            del result[start:]
            result.extend(rows)
            if trailing is not None:
                result.append(trailing)

        def ternary_shape(found):
            # bounds shape: CONST below, VAR middle, CONST on top
            hi_op = result[found[0]][0]
            mid_op = result[found[1]][0]
            lo_op = result[found[2]][0]
            return (lo_op == OpCode.KOS_OP_CONST
                    and mid_op == OpCode.KOS_OP_VAR
                    and hi_op == OpCode.KOS_OP_CONST)

        for op, arg in instr:
            if op == OpCode.KOS_OP_CHECK_COMPARE and arg[1] == 2:
                found, pos_rows = scan_back(2)
                if found:
                    y_op, y_arg = result[found[0]]
                    x_op, x_arg = result[found[1]]
                    x_const = x_op == OpCode.KOS_OP_CONST
                    y_const = y_op == OpCode.KOS_OP_CONST
                    if not (x_const and y_const):
                        sel, _cnt, label, _pad = arg
                        splice(found[1], pos_rows, [(
                            OpCode.KOS_OP_CHECK_COMPARE_VAR,
                            (make_cmpsrc(sel, x_const, y_const),
                             x_arg, y_arg, label))])
                        continue

            elif op == OpCode.KOS_OP_CHECK_COMPARE and arg[1] == 3:
                found, pos_rows = scan_back(3)
                if found and ternary_shape(found):
                    sel, _cnt, label, _pad = arg
                    lo = result[found[2]][1]
                    mid = result[found[1]][1]
                    hi = result[found[0]][1]
                    splice(found[2], pos_rows, [
                        (OpCode.KOS_OP_OBJECT_COMPARE_VAR,
                         (sel, lo, mid, hi)),
                        (OpCode.KOS_OP_JUMP_FALSE, label),
                    ])
                    continue

            elif op == OpCode.KOS_OP_OBJECT_COMPARE:
                sel, cnt = split_cmpcount(arg)
                if cnt == 3:
                    found, pos_rows = scan_back(3)
                    if found and ternary_shape(found):
                        lo = result[found[2]][1]
                        mid = result[found[1]][1]
                        hi = result[found[0]][1]
                        splice(found[2], pos_rows, [(
                            OpCode.KOS_OP_OBJECT_COMPARE_VAR,
                            (sel, lo, mid, hi))])
                        continue

            result.append((op, arg))

        return result


    def pack_var_runs(self, instr):
        """
        Late sweep: combine runs of consecutive bare VAR loads into VAR4 /
        VAR2 rows (greedy, in that order). Runs after every other squash so
        consumer-specific fusions get first claim on their feeder rows, and
        breaks at any non-VAR instruction (including LABEL). POSITION rows
        inside a run are absorbed -- the merged row keeps the position in
        effect at the run's first load, and the latest absorbed position is
        re-emitted after the run so following instructions keep theirs. The
        fast pass later lowers VAR2/VAR4 to FAST2/FAST4 through the
        ordinary VAR->FAST path.
        """

        result = []
        run = []
        trailing_pos = None

        def flush():
            nonlocal trailing_pos
            while len(run) >= 4:
                a, b, c, d = run[:4]
                del run[:4]
                result.append((OpCode.KOS_OP_VAR4, (a, b, c, d)))
            if len(run) >= 2:
                a, b = run[:2]
                del run[:2]
                result.append((OpCode.KOS_OP_VAR2, (a, b, None, None)))
            if run:
                result.append((OpCode.KOS_OP_VAR, run.pop()))
            if trailing_pos is not None:
                result.append(trailing_pos)
                trailing_pos = None

        for op, arg in instr:
            if op == OpCode.KOS_OP_VAR:
                run.append(arg)
                continue
            if run and op == OpCode.KOS_OP_POSITION:
                trailing_pos = (op, arg)
                continue
            flush()
            result.append((op, arg))

        flush()
        return result


    def normalize_opcount(self, instr):
        """
        Merge every KOSTYLE_OPCOUNT and KOSTYLE_CMPCOUNT operand to its
        packed uint16. Specials may emit a (selector, count) pair or a
        pre-merged int; the merge helpers accept and validate both. Runs
        unconditionally, independent of the optional convert_fast lowering
        (which only relabels VAR->FAST and leaves non-VAR fields, including
        merged OPCOUNT/CMPCOUNT args, untouched).
        """

        result = []
        for op, arg in instr:
            if op.style == OpStyle.KOSTYLE_OPCOUNT:
                arg = merge_opcount(arg)
            elif op.style == OpStyle.KOSTYLE_CMPCOUNT:
                arg = merge_cmpcount(arg)
            elif (op.style == OpStyle.KOSTYLE_EXTRA
                  and OpStyle.KOSTYLE_OPCOUNT in op.extra):
                arg = tuple(
                    merge_opcount(v) if fk == OpStyle.KOSTYLE_OPCOUNT else v
                    for v, fk in zip(arg, op.extra))
            result.append((op, arg))
        return result


    def convert_fast(self, base_nspec, instr, mode=None):
        if mode is None:
            mode = self.mode

        result = []
        nstack = []
        findex = []

        def push_nspec(n):
            nstack.append(n)
            findex.clear()
            findex.extend(n.local_syms)
            findex.extend(n.closure_syms)
            findex.extend(n.global_syms)

        def pop_nspec():
            nstack.pop()
            findex.clear()
            if nstack:
                n = nstack[-1]
                findex.extend(n.local_syms)
                findex.extend(n.closure_syms)
                findex.extend(n.global_syms)

        if mode == CompilerMode.FUNCTION:
            push_nspec(base_nspec)

        def fast_slot(name):
            # Return the fast slot index for name, or -1 if it has no fast
            # slot (not in the pool, or pool is at capacity).
            try:
                idx = findex.index(name)
            except ValueError:
                return -1
            return idx if idx < NAMESPACE_FAST_SLOT_MAX else -1

        nspec = None
        for op, arg in instr:
            if op == OpCode.KOS_OP_CONST:
                if isinstance(arg, NamespaceSpec):
                    nspec = arg

            elif op == OpCode.KOS_OP_PUSH_NS:
                push_nspec(arg[0])
                nspec = None

            elif op == OpCode.KOS_OP_PUSH_COLLECT:
                push_nspec(arg if isinstance(arg, NamespaceSpec) else nspec)
                nspec = None

            elif op in (OpCode.KOS_OP_POP_NS, OpCode.KOS_OP_POP_COLLECT):
                pop_nspec()

            elif nstack and op == OpCode.KOS_OP_CHECK_COMPARE_VAR:
                # CMPSRC row: slot kinds are data-dependent (a set source
                # bit means CONST, else the declared VAR kind), so the
                # generic schema-driven lowering below cannot apply. Lower
                # each name-sourced slot to its fast index; all-or-nothing
                # as usual, const-bit slots pass through untouched.
                cmpsrc, x, y, label = arg
                nx = x if (cmpsrc & CMPSRC_X_CONST) else fast_slot(x)
                ny = y if (cmpsrc & CMPSRC_Y_CONST) else fast_slot(y)
                if (((cmpsrc & CMPSRC_X_CONST) or nx >= 0)
                        and ((cmpsrc & CMPSRC_Y_CONST) or ny >= 0)):
                    op = OpCode.KOS_OP_CHECK_COMPARE_FAST
                    arg = (cmpsrc, nx, ny, label)

            elif nstack and op in VAR_TO_FAST:
                # If opcode style is EXTRA, use op.extra as the schema tuple
                # (opcodes.incl field kinds). Zip with the operand 4-tuple; each
                # KOSTYLE_VAR slot is lowered to a fast index if one is
                # available; otherwise that field stays as the original value
                # and the whole opcode stays as VAR.
                new_op = VAR_TO_FAST[op]
                if op.style == OpStyle.KOSTYLE_EXTRA:
                    new_arg = tuple([
                        fast_slot(v) if fk == OpStyle.KOSTYLE_VAR else v
                        for v, fk in zip(arg, op.extra)])
                    if all(
                            idx >= 0
                            for v, fk, idx in zip(arg, op.extra, new_arg)
                            if fk == OpStyle.KOSTYLE_VAR):
                        arg = new_arg
                        op = new_op
                else:
                    idx = fast_slot(arg)
                    if idx >= 0:
                        arg = idx
                        op = new_op

            result.append((op, arg))

        return result


    def demote_tail_calls_if_yields(self, instr):
        """
        If the stream contains yield or yield-from, replace TAIL_CALL* with
        CALL* so generator frames never use tail-call frame reuse. Native
        Code lowering rejects the combination outright.
        """

        yield_ops = (OpCode.KOS_OP_YIELD, OpCode.KOS_OP_YIELD_FROM)
        if not any(op in yield_ops for op, _ in instr):
            return instr
        return [
            (_TAIL_CALL_TO_CALL.get(op, op), arg)
            for op, arg in instr]


    def _cpool_scan_merge(self, fragment, instr):
        """
        Scan instructions for CONST (FunctionSpec) and merge inner Code's
        closure_syms and request_for_write_syms into the fragment.
        """

        reso = fragment.resolution
        for op, arg in instr:
            if op == OpCode.KOS_OP_CONST and isinstance(arg, FunctionSpec):
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

        # Flatten position/label instructions and merge closure/write syms
        # from inner FunctionSpecs into the fragment
        instr = self.compress(fragment.instructions)
        # Pack OPCOUNT operands before anything downstream reads the arg;
        # unconditional, unlike the optional convert_fast lowering below.
        instr = self.normalize_opcount(instr)
        self._cpool_scan_merge(fragment, instr)

        # Build NamespaceSpec from fragment buckets (local, closure, global)
        reso = fragment.resolution
        base_nspec = reso.build_namespace_spec(fragment)

        # Lower VAR/request ops to FAST where possible
        if self.convert_fast_enabled:
            instr = self.convert_fast(base_nspec, instr, mode=mode)

        # Generators are not allowed to tail-call
        instr = self.demote_tail_calls_if_yields(instr)

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
