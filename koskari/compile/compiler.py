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


from contextlib import contextmanager
from enum import auto, IntEnum
from typing import Protocol

from ..lib import (
    cons,
    keyword,
    nil,
    symbol,
    Code,
    FormalsSpec,
    FunctionSpec,
    Module,
    ModuleNamespace,
    Namespace,
    NamespaceSpec,
    OpCode,
    OpStyle,
)

from ..types import (
    is_pair,
    is_proper,
    is_symbol,
    get_position,
)

from ..exceptions import CompilerError, UncaughtCompilerError
from .pragma import KoskariPragma


class Compiled(Protocol):

    def compile_impl(self, compiler, source_obj, tc):
        ...


class Special(Compiled):

    def __init__(self, name, compile_impl, aliases=()):
        self.name = name
        self.aliases = aliases
        self.forms = []
        self._impl = compile_impl

    def compile_impl(self, compiler, source_obj, tc):
        return self._impl(compiler, source_obj, tc)


class Alias(Compiled):

    def __init__(self, name, compile_impl):
        self.name = name
        self._impl = compile_impl

    def compile_impl(self, compiler, source_obj, tc):
        return self._impl(compiler, source_obj, tc)


def _is_special(v):
    return isinstance(v, Special) or hasattr(v, "compile_impl")


def _is_alias(v):
    # TODO: we'll likely want to add a distinct type for aliases.
    # for now aliasing is disabled in compile_symbol
    return isinstance(v, Alias)


def _is_lazygensym(x):
    return False


CONST_TYPES = (
    str, bytes,
    bool, int, float, complex,
    slice,
    type(None), type(...),
    symbol, keyword, type(nil),
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
    return var_to_fast


VAR_TO_FAST = _build_var_to_fast()


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
_symbol_ellipsis = symbol("ellipsis")
_symbol_nil = symbol("nil")


def unique_append(lst, val):
    """
    Append val to lst only if it is not already present

    :param lst: a mutable sequence
    :param val: value to append
    """

    if val not in lst:
        lst.append(val)


class ByteCodeFragment:
    """
    Holds a fragment of bytecode during compilation: instructions plus
    closure requests and name declarations for the namespace
    """

    def __init__(self, compiler=None, name=None, filename=None, mode=None,
                 pragma=None):
        """
        :param compiler: the compiler/builder used to complete this fragment
        :param name: optional fragment name (e.g. for a block or loop)
        :param filename: optional source filename
        :param mode: optional compilation mode
        :param pragma: resolution pragma; default KoskariPragma when omitted
        """

        self.name = name
        self.filename = filename
        self.mode = mode
        self.compiler = compiler
        self.pragma = pragma if pragma is not None else KoskariPragma()
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

        return self.pragma.build_namespace_spec(self)


    def request_var(self, var_name):
        """
        Record a closure request for var_name unless it is already declared

        :param var_name: name to request from enclosing scope
        """

        self.pragma.on_request_var(self, str(var_name))


    def request_for_write(self, var_name, position=None):
        """
        Record that var_name must be writable (setq, delq, inplace).

        :param var_name: name that must be writable
        :param position: optional (line, col) for error reporting
        """

        self.pragma.on_write(self, str(var_name), position=position)


    def request_global(self, var_name, position=None):
        """
        Record explicit need from module (define-global).

        :param var_name: name to pull from module
        :param position: optional (line, col) for error reporting
        """

        self.pragma.on_explicit_global(self, str(var_name), position=position)


    def declare_var(self, var_name, position=None, type_expr=None):
        """
        Record a name declared in this fragment's namespace.

        :param var_name: name to declare
        :param position: optional (line, col) for error reporting
        :param type_expr: optional source form for a type annotation (compile
            metadata only; not evaluated here)
        """

        self.pragma.on_declare(self, str(var_name), position=position)
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
        with compiler.pragma_scope():
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
        with compiler.pragma_scope():
            ns_name = str(ns_name)
            child = compiler.fragment(ns_name)

            yield child
            nspec = child.as_namespace_spec()

            self.instruction(OpCode.KOS_OP_PUSH_COLLECT, nspec)
            self.instructions.extend(child.instructions)
            self.instruction(OpCode.KOS_OP_POP_COLLECT, 0)


    @contextmanager
    def child_loop(self, named="<loop>"):
        """
        Context manager that compiles a loop body. On exit, emits
        PUSH_LOOP (break target derived from extent table at runtime),
        the child's instructions, POP_LOOP, LABEL after_pop. The child
        must end with LABEL and CLEAR_LOOP for normal exit. The first
        instruction after PUSH_LOOP is the continue target.
        """

        child = self.compiler.fragment(named)
        yield child
        after_pop = self.compiler.gen_label("loop.after_pop")
        self.instruction(OpCode.KOS_OP_PUSH_LOOP, 0)
        self.merge(child)
        self.instruction(OpCode.KOS_OP_POP_LOOP, 0)
        self.instruction(OpCode.KOS_OP_LABEL, after_pop)


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
    OpCode.KOS_OP_OBJECT_GE: OpCode.KOS_OP_CHECK_GE,
    OpCode.KOS_OP_OBJECT_LE: OpCode.KOS_OP_CHECK_LE,
    OpCode.KOS_OP_OBJECT_LT: OpCode.KOS_OP_CHECK_LT,
    OpCode.KOS_OP_OBJECT_GT: OpCode.KOS_OP_CHECK_GT,
    OpCode.KOS_OP_OBJECT_EQ: OpCode.KOS_OP_CHECK_EQ,
    OpCode.KOS_OP_OBJECT_NE: OpCode.KOS_OP_CHECK_NE,
    OpCode.KOS_OP_OBJECT_IN: OpCode.KOS_OP_CHECK_IN,
    OpCode.KOS_OP_OBJECT_NOT_IN: OpCode.KOS_OP_CHECK_NOT_IN,
    OpCode.KOS_OP_OBJECT_IS: OpCode.KOS_OP_CHECK_IS,
    OpCode.KOS_OP_OBJECT_IS_NOT: OpCode.KOS_OP_CHECK_IS_NOT,
    OpCode.KOS_OP_OBJECT_IS_INSTANCE: OpCode.KOS_OP_CHECK_IS_INSTANCE,
    OpCode.KOS_OP_OBJECT_IS_SUBCLASS: OpCode.KOS_OP_CHECK_IS_SUBCLASS,
    OpCode.KOS_OP_OBJECT_NOT: OpCode.KOS_OP_JUMP_TRUE,
    OpCode.KOS_OP_EXCEPTION_MATCH: OpCode.KOS_OP_CHECK_EXCEPTION,
}


class Compiler:

    def __init__(self, name, module, filename=None, tco_enabled=True,
                 resolution_pragma_class=None, resolution_pragma=None):
        self.name = name
        self.resolution_pragma_class = resolution_pragma_class or KoskariPragma
        self._current_pragma = resolution_pragma

        if module is None:
            module = Module(ModuleNamespace(name, {}))
        elif isinstance(module, dict):
            module = Module(ModuleNamespace(name, module))
        elif isinstance(module, Namespace):
            module = Module(module)

        self.module = module

        self.tco_enabled = tco_enabled
        self.gen_label = _label_generator()

        ns = vars(module)

        r = ns.ref("__compiler__")
        if r.empty:
            r.store(self)

        r = ns.ref("__name__")
        if r.empty:
            r.store(name)
        elif name is None:
            name = r.deref()
        self.name = name

        r = ns.ref("__path__")
        if r.empty:
            r.store(filename)
        elif filename is None:
            filename = r.deref()
        self.filename = filename


    def error(self, msg, source):
        raise CompilerError(msg, source)


    @contextmanager
    def pragma_scope(self):
        pragma = self._current_pragma
        if pragma is not None:
            pragma.enter_scope()
        try:
            yield
        finally:
            if pragma is not None:
                pragma.exit_scope()


    def _find_compiled(self, name):
        ns = vars(self.module)

        try:
            r = ns.ref(name)
        except (KeyError, AttributeError, TypeError):
            return None

        if r is None or r.empty:
            return None

        v = r.deref()
        if _is_special(v):
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
                name = "compile/" + name
                return self._find_compiled(name)
        return found


    def _find_layout(self, name):
        from ..reader.layouts.helpers import LayoutHandler

        ns = vars(self.module)
        try:
            r = ns.ref(name)
        except (KeyError, AttributeError, TypeError):
            return None

        if r is None or r.empty:
            return None

        v = r.deref()
        if isinstance(v, LayoutHandler):
            return v
        return None


    def find_layout(self, name):
        """
        Resolve layout/<head> in the module namespace

        :param name: symbol or string for the block head (e.g. if, begin)
        :return: LayoutHandler instance, or None
        """

        name = str(name)

        found = self._find_layout(name)
        if found is None:
            if not name.startswith("layout/"):
                name = "layout/" + name
                return self.find_layout(name)
        return found


    def fragment(self, name=None, mode=None):
        pragma = self._current_pragma
        if pragma is None:
            pragma = self.resolution_pragma_class()
        return ByteCodeFragment(
            self, str(name), self.filename, mode=mode, pragma=pragma)


    def compile(self, source_obj, tc=False):
        if isinstance(source_obj, ByteCodeFragment):
            return source_obj

        allocated = self._current_pragma is None
        if allocated:
            self._current_pragma = self.resolution_pragma_class()
        try:
            fragment = self.fragment(self.name)
            return self.compile_expr(fragment, source_obj, tc) or fragment
        finally:
            if allocated:
                self._current_pragma = None


    def full_compile(self, source_obj, tc=False):
        allocated = self._current_pragma is None
        if allocated:
            self._current_pragma = self.resolution_pragma_class()
        try:
            res = source_obj
            while not isinstance(res, ByteCodeFragment):
                res = self.compile(res, tc)
            return res
        finally:
            if allocated:
                self._current_pragma = None


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

        # comp = self.find_compiled(sym)
        # if comp and _is_alias(comp):
        #     return self.compile_special(fragment, comp, sym, tc)
        return self.compile_raw_symbol(fragment, sym, tc)


    def compile_raw_symbol(self, fragment, sym, tc):
        if sym is _symbol_None or str(sym) == "None":
            return self.compile_const(fragment, None, tc)

        if sym is _symbol_True or str(sym) == "True":
            return self.compile_const(fragment, True, tc)

        if sym is _symbol_False or str(sym) == "False":
            return self.compile_const(fragment, False, tc)

        if sym is _symbol_ellipsis or str(sym) == "ellipsis":
            return self.compile_const(fragment, ..., tc)

        if sym is _symbol_nil or str(sym) == "nil":
            return self.compile_const(fragment, nil, tc)

        if _is_lazygensym(sym):
            fragment.request_var(sym)
            fragment.instruction(OpCode.KOS_OP_VAR, sym)
            return fragment

        ex = sym.rsplit(".", 1)
        if len(ex) == 1:
            sym = str(sym)
            fragment.request_var(sym)
            fragment.instruction(OpCode.KOS_OP_VAR, sym)
            return fragment

        source = cons(_symbol_attr, *ex, nil)
        return self.compile_pair(fragment, source, tc)


    def compile_pair(self, fragment, source_obj, tc):
        if source_obj is nil:
            return self.compile_const(fragment, nil, False)

        fragment.position_of(source_obj)

        if not is_proper(source_obj):
            raise CompilerError(
                "Cannot evaluate improper lists as expressions",
                source_obj)

        head, tail = source_obj

        if is_symbol(head) or _is_lazygensym(head):
            comp = self.find_compiled(head)
            if comp:
                return self.compile_special(fragment, comp, source_obj, tc)

            if comp is symbol('compile/call'):
                raise CompilerError("compile/call not found", source_obj)

            expr = cons(symbol('compile/call'), head, tail)
            return expr

        else:
            head = self.compile(head)

        if isinstance(head, ByteCodeFragment):
            expr = cons(symbol('compile/call'), head, tail)
            return expr

        expr = cons(head, tail)
        return expr


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
                prior = squished[-1][0]
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

        return squished


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

            elif nstack and op in VAR_TO_FAST:
                # If opcode style is EXTRA, use op.extra as the schema tuple
                # (opcodes.incl field kinds). Zip with the operand 4-tuple; each
                # KOSTYLE_VAR slot is lowered to a fast index. Scalar KOSTYLE_VAR
                # opargs use findex on the whole arg. Any failure skips lowering.
                try:
                    new_op = VAR_TO_FAST[op]
                    if op.style == OpStyle.KOSTYLE_EXTRA:
                        new_arg = tuple([
                            findex.index(v)
                            if fk == OpStyle.KOSTYLE_VAR
                            else v
                            for v, fk in zip(arg, op.extra)])
                    else:
                        new_arg = findex.index(arg)
                except Exception:
                    pass
                else:
                    arg = new_arg
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

        pragma = fragment.pragma
        for op, arg in instr:
            if op == OpCode.KOS_OP_CONST and isinstance(arg, FunctionSpec):
                code = arg.code
                if code is None:
                    continue
                nspec = code.namespace_spec
                if nspec is None:
                    continue
                request_for_write_syms = pragma.get_request_for_write_syms(code)
                pragma.cpool_merge(fragment, nspec, request_for_write_syms)


    def complete(self, fragment, mode=None, _validate=True, is_root=False):
        if mode is None:
            mode = self.mode
        # Flatten position/label instructions and merge closure/write syms
        # from inner FunctionSpecs into the fragment
        instr = self.compress(fragment.instructions)
        self._cpool_scan_merge(fragment, instr)

        # Build NamespaceSpec from fragment buckets (local, closure, global)
        pragma = fragment.pragma
        base_nspec = pragma.build_namespace_spec(fragment)

        # Lower VAR/request ops to FAST where possible
        instr = self.convert_fast(base_nspec, instr, mode=mode)

        instr = self.demote_tail_calls_if_yields(instr)

        # Resolve filename for Code metadata
        try:
            path = self.module.__path__
        except AttributeError:
            path = None
        filename = (fragment.filename or path or self.filename or None)

        # Build Code and register with pragma for position lookup on errors
        code = Code(base_nspec, instr, fragment.name, filename)
        if fragment.doc is not None:
            code.extras['doc'] = fragment.doc
        pragma.register_code_fragment(code, fragment)
        # Recursively validate that all writes are satisfied.
        # Only validate from root complete(); inner complete() calls skip so
        # nested functions get validated with proper ancestor context.
        if _validate and is_root:
            pragma.validate_tree(
                code, (), CompilerMode.FUNCTION, is_root=True)
        return code


# The end.
