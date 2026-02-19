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
from typing import Any

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
)

from ..types import (
    is_pair,
    is_proper,
    is_symbol,
    get_position,
    fill_position,
)

from .exceptions import CompilerException, UncaughtCompilerException


def _is_compiled(v):
    return hasattr(v, 'compile_impl') and callable(
        getattr(v, 'compile_impl', None))


def _is_alias(v):
    return _is_compiled(v)


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

    def __init__(self, compiler=None, name=None, filename=None, mode=None):
        """
        :param compiler: the compiler/builder used to complete this fragment
        :param name: optional fragment name (e.g. for a block or loop)
        :param filename: optional source filename
        :param mode: optional compilation mode
        """

        self.name = name
        self.filename = filename
        self.mode = mode
        self.compiler = compiler
        self.instructions = []
        self.requests = []
        self.declares = []


    def as_namespace_spec(self):
        """
        Build a NamespaceSpec from this fragment's name and declarations.
        Local names are declares; closure names are requests.

        :return: NamespaceSpec(name, declares, requests)
        """

        return NamespaceSpec(
            self.name,
            tuple(self.declares),
            tuple(self.requests),
        )


    def request_var(self, var_name):
        """
        Record a closure request for var_name unless it is already declared

        :param var_name: name to request from enclosing scope
        """

        var_name = str(var_name)
        if var_name not in self.declares:
            unique_append(self.requests, var_name)


    def declare_var(self, var_name):
        """
        Record a name declared in this fragment's namespace

        :param var_name: name to declare
        """

        unique_append(self.declares, str(var_name))


    def declare_vars(self, var_names):
        """
        Record multiple names declared in this fragment's namespace

        :param var_names: iterable of names to declare
        """

        for var_name in var_names:
            unique_append(self.declares, str(var_name))


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
        Merge another fragment into this one: adopt its declarations and
        requests, then append its instructions

        :param other: ByteCodeFragment to merge in
        """

        for name in other.declares:
            self.declare_var(name)
        for name in other.requests:
            self.request_var(name)
        self.instructions.extend(other.instructions)


    @contextmanager
    def child_namespace(self, ns_name, names, values, pop=1):
        """
        Context manager that compiles a child scope with bound names.
        On entry, compiles all value expressions and yields a fragment for
        the body. On exit, emits CONST nspec, values, CONST names,
        PUSH_NS, body instructions, POP_NS.

        :param ns_name: name for the child namespace (e.g. source form)
        :param names: sequence of bound names (strings or symbols)
        :param values: sequence of expressions (one per name)
        :param pop: argument for POP_NS (default 1)
        """

        compiler = self.compiler
        ns_name = str(ns_name)
        child = compiler.fragment(ns_name)
        names = tuple(map(str, names))
        if len(names) != len(values):
            raise CompilerException("mismatch between names and values",
                                   (names, values))
        if names:
            for name in names:
                child.declare_var(name)
            valfrag = compiler.fragment(ns_name)
            for val in values:
                valfrag.merge(compiler.full_compile(val, False))
        yield child
        nspec = child.as_namespace_spec()
        self.instruction(OpCode.KOS_OP_CONST, nspec)
        if names:
            self.merge(valfrag)
            self.instruction(OpCode.KOS_OP_CONST, names)
        self.instruction(OpCode.KOS_OP_PUSH_NS, len(names))
        self.instructions.extend(child.instructions)
        self.instruction(OpCode.KOS_OP_POP_NS, pop)


    @contextmanager
    def child_loop(self, named="<loop>"):
        """
        Context manager that compiles a loop body. On exit, emits
        PUSH_LOOP (break target = after_pop), the child's instructions,
        POP_LOOP, LABEL after_pop. The child must end with LABEL and
        CLEAR_LOOP for normal exit. The first instruction after PUSH_LOOP
        is the continue target.
        """

        child = self.compiler.fragment(named)
        yield child
        after_pop = self.compiler.gen_label("loop.after_pop")
        self.instruction(OpCode.KOS_OP_PUSH_LOOP, after_pop)
        self.merge(child)
        self.instruction(OpCode.KOS_OP_POP_LOOP, 0)
        self.instruction(OpCode.KOS_OP_LABEL, after_pop)


    def as_bytecode(self):
        if self.compiler is None:
            raise TypeError("ByteCodeFragment has no compiler")
        return self.compiler.complete(self)


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
        code = self.compiler.complete(self, mode=CompilerMode.EXPRESSION)
        return code.expression(parent)


class CompilerMode(IntEnum):
    FUNCTION = auto()
    EXPRESSION = auto()


class Compiler:

    def __init__(self, name, module, filename=None, tco_enabled=True):
        self.name = name

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
        raise CompilerException(msg, source)


    def find_compiled(self, name):
        name = str(name)
        if not name.startswith("compile/"):
            name = "compile/" + name

        ns = vars(self.module)
        try:
            r = ns.ref(name)
        except (KeyError, AttributeError, TypeError):
            return None

        if r is None or r.empty:
            return None

        v = r.deref()
        if _is_compiled(v):
            return v
        return None


    def fragment(self, name=None, mode=None):
        return ByteCodeFragment(self, str(name), self.filename, mode=mode)


    def compile(self, source_obj, tc=False):
        if isinstance(source_obj, ByteCodeFragment):
            return source_obj

        fragment = self.fragment(self.name)
        return self.compile_expr(fragment, source_obj, tc)


    def full_compile(self, source_obj, tc=False):
        res = source_obj
        while not isinstance(res, ByteCodeFragment):
            res = self.compile(res, tc)
        return res


    def compile_expr(self, fragment, source_obj, tc):
        tc = self.tco_enabled and tc

        if is_symbol(source_obj):
            dispatch = self.compile_symbol
        elif is_pair(source_obj):
            dispatch = self.compile_pair
        elif _is_const(source_obj):
            dispatch = self.compile_const
        else:
            raise CompilerException("Unsupported source object", source_obj)

        try:
            return dispatch(fragment, source_obj, tc) or fragment
        except (CompilerException,):
            raise
        except Exception as ex:
            from ..exceptions import KoskariSyntaxError
            if isinstance(ex, KoskariSyntaxError):
                raise
            raise UncaughtCompilerException(ex, source_obj) from ex


    def compile_const(self, fragment, value, tc):
        fragment.instruction(OpCode.KOS_OP_CONST, value)
        return fragment


    def compile_symbol(self, fragment, sym, tc):
        comp = self.find_compiled(sym)
        if comp and _is_alias(comp):
            return self.compile_special(fragment, comp, sym, tc)
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

        ex = str(sym).rsplit(".", 1)
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
            raise CompilerException("Cannot evaluate improper lists as"
                                    " expressions", source_obj)

        head, tail = source_obj

        if is_symbol(head) or _is_lazygensym(head):
            comp = self.find_compiled(head)
            if comp:
                return self.compile_special(fragment, comp, source_obj, tc)
            head = self.compile_raw_symbol(fragment, head, False)
        else:
            head = self.compile(head)

        if isinstance(head, ByteCodeFragment):
            expr = cons(symbol('compile/call'), head, tail)
            if hasattr(source_obj, 'set_position'):
                expr.set_position(source_obj.get_position())
            return expr

        expr = cons(head, tail)
        if hasattr(source_obj, 'set_position'):
            expr.set_position(source_obj.get_position())
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
            if ((op == OpCode.KOS_OP_POP and
                 arg == 1 and
                 squished[-1][0] in (OpCode.KOS_OP_CONST,
                                     OpCode.KOS_OP_DUP))):
                del squished[-1]
                continue

            elif (op == OpCode.KOS_OP_JUMP_TRUE and
                  squished[-1][0] == OpCode.KOS_OP_CONST):
                truthy = bool(squished[-1][1])
                del squished[-1]
                if truthy:
                    op = OpCode.KOS_OP_JUMP
                else:
                    continue

            elif (op == OpCode.KOS_OP_JUMP_FALSE and
                  squished[-1][0] == OpCode.KOS_OP_CONST):
                truthy = not squished[-1][1]
                del squished[-1]
                if truthy:
                    op = OpCode.KOS_OP_JUMP
                else:
                    continue

            elif op == OpCode.KOS_OP_LABEL:
                prior = squished[-1]
                if (prior[0] == OpCode.KOS_OP_JUMP and
                        prior[1] == arg):
                    del squished[-1]

                elif (prior[0] in (OpCode.KOS_OP_JUMP_TRUE,
                                  OpCode.KOS_OP_JUMP_FALSE) and
                      prior[1] == arg):
                    if len(squished) > 1 and squished[-2][0] == OpCode.KOS_OP_CONST:
                        del squished[-1]
                        del squished[-1]
                    else:
                        squished[-1] = (OpCode.KOS_OP_POP, 1)

            elif ((op in (OpCode.KOS_OP_RETURN,
                          OpCode.KOS_OP_BREAK,
                          OpCode.KOS_OP_CONTINUE,
                          OpCode.KOS_OP_YIELD) and
                   arg == 1 and
                   squished[-1][0] == OpCode.KOS_OP_CONST and
                   squished[-1][1] is None)):
                del squished[-1]
                arg = 0

            squished.append((op, arg))

        if squished[-1][0] != OpCode.KOS_OP_RETURN:
            squished.append((OpCode.KOS_OP_RETURN, 1))

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
            locals_syms = getattr(n, 'local_syms', None) or ()
            closure_syms = getattr(n, 'closure_syms', None) or ()
            findex.extend(locals_syms)
            findex.extend(closure_syms)

        def pop_nspec():
            nstack.pop()
            findex.clear()
            if nstack:
                n = nstack[-1]
                locals_syms = getattr(n, 'local_syms', None) or ()
                closure_syms = getattr(n, 'closure_syms', None) or ()
                findex.extend(locals_syms)
                findex.extend(closure_syms)

        if mode == CompilerMode.FUNCTION:
            push_nspec(base_nspec)

        translate = OpCode.KOS_OP_FAST - OpCode.KOS_OP_VAR

        nspec = None
        for op, arg in instr:
            if op == OpCode.KOS_OP_CONST:
                if isinstance(arg, NamespaceSpec):
                    nspec = arg

            elif op in (OpCode.KOS_OP_PUSH_NS, OpCode.KOS_OP_PUSH_CLASS):
                push_nspec(nspec)
                nspec = None

            elif op == OpCode.KOS_OP_POP_NS:
                pop_nspec()

            elif nstack and (OpCode.KOS_OP_VAR <= op <= OpCode.KOS_OP_VAR_STORE):
                try:
                    index = findex.index(arg)
                except ValueError:
                    pass
                else:
                    op = OpCode(op + translate)
                    arg = index

            result.append((op, arg))

        return result


    def complete(self, fragment, mode=None):
        instr = self.compress(fragment.instructions)
        base_nspec = fragment.as_namespace_spec()

        instr = self.convert_fast(base_nspec, instr, mode=mode)

        filename = (fragment.filename
                    or getattr(self.module, '__path__', None)
                    or self.filename
                    or None)

        return Code(base_nspec, instr, fragment.name, filename)


# The end.
