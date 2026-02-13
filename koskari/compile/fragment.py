"""
koskari.compile.fragment

Bytecode fragment type for koskari compiler.

author: Christopher O'Brien <obriencj@preoccupied.net>
license: GNU General Public License v3
"""


from ..lib import FunctionSpec
from ..types import FormalsSpec, NamespaceSpec, OpCode, fill_position, get_position, is_pair, set_position

from .exceptions import CompilerMode


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


# The end.
