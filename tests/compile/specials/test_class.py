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
Tests for koskari.compile compile/class special form.
"""


from koskari.dis import dis
from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestClassEmpty(SpecialsTestCase):

    async def test_empty_class_binds_and_is_type(self):
        src_str = """
        (compile/class C () 5)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsInstance(result, type)
        self.assertEqual(result.__name__, "C")


    async def test_empty_class_is_kostype(self):
        from koskari.lib import KosType

        src_str = """
        (compile/class C () 5)
        """
        expr, mod = self.compile_expr(src_str)

        dis(expr.code)

        result = await expr()
        self.assertIs(type(result), KosType)

        c = result()
        self.assertIsInstance(c, result)


class TestClassQualname(SpecialsTestCase):

    async def test_class_has_qualname_from_form_name(self):
        src_str = """
        (compile/begin
          (compile/define C (compile/class C () (compile/define x 1)))
          (compile/attr C __qualname__))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "C")

    async def test_class_qualname_not_overwritten_if_declared(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C ()
              (compile/define __qualname__ "custom")
              (compile/define x 1)))
          (compile/attr C __qualname__))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "custom")


class TestClassModule(SpecialsTestCase):

    async def test_class_has_module_from_compiling_context(self):
        src_str = """
        (compile/begin
          (compile/define C (compile/class C () (compile/define x 1)))
          (compile/attr C __module__))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "test")

    async def test_class_module_not_overwritten_if_declared(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C ()
              (compile/define __module__ "custom.mod")
              (compile/define x 1)))
          (compile/attr C __module__))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "custom.mod")


class TestClassDocstring(SpecialsTestCase):

    async def test_class_has_docstring_from_leading_string(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C ()
              "hello class doc"
              (compile/define x 1)))
          (compile/attr C __doc__))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "hello class doc")


    async def test_class_docstring_only_body(self):
        src_str = """
        (compile/begin
          (compile/define C (compile/class C () "only doc"))
          (compile/attr C __doc__))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "only doc")


class TestClassAnnotations(SpecialsTestCase):

    async def test_class_pep526_annotations_dict(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C ()
              (compile/define x :: int 1)))
          C)
        """
        expr, mod = self.compile_expr(src_str, int=int)
        C = await expr()
        self.assertEqual(C.__annotations__, {"x": int})

    async def test_class_untyped_define_no_annotations_emitted(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C ()
              (compile/define x 1)))
          C)
        """
        expr, mod = self.compile_expr(src_str)
        C = await expr()
        self.assertEqual(getattr(C, "__annotations__", {}), {})


    async def test_class_intenum_with_typed_define(self):
        from enum import IntEnum

        src_str = """
        (compile/begin
          (compile/define E
            (compile/class E (IntEnum)
              (compile/define foo :: int 1)))
          E)
        """
        expr, mod = self.compile_expr(src_str, IntEnum=IntEnum, int=int)
        E = await expr()
        self.assertTrue(issubclass(E, IntEnum))
        self.assertEqual(E.foo.value, 1)
        self.assertEqual(E.__annotations__, {'foo': int})
        self.assertFalse(hasattr(E, 'int'))


class TestClassWithAttribute(SpecialsTestCase):

    async def test_class_with_define(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C ()
              (compile/define x 42)))
          C.x)
        """
        expr, mod = self.compile_expr(src_str)

        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


class TestClassWithSuperclass(SpecialsTestCase):

    async def test_class_extends_object(self):
        src_str = """
        (compile/begin
          (compile/define C
            (compile/class C (object)
              (compile/define x 1)))
          C.x)
        """

        print(src_str)

        expr, mod = self.compile_expr(src_str, object=object)

        dis(expr.code)

        result = await expr()
        self.assertEqual(1, result)


class TestClassWithInitAndMethods(SpecialsTestCase):

    async def test_class_init_getter_setter_and_direct_attr(self):
        """
        Box has __init__(self, v), get_value(self), set_value(self, new_val).
        Create instance via (Box 10), verify getter and direct attr, set
        to 20, verify both track. Method calls return awaitables; use await.
        """
        src_str = """
        (compile/begin
          (compile/define Box
            (compile/class Box ()
              (compile/define __init__
                (compile/lambda [self v]
                  (compile/set-attr self value v)))
              (compile/define get_value
                (compile/lambda [self]
                  (compile/attr self value)))
              (compile/define set_value
                (compile/lambda [self new_val]
                  (compile/set-attr self value new_val)))))
          (Box 10))
        """
        expr, mod = self.compile_expr(src_str)
        instance = await expr()
        self.assertEqual(await instance.get_value(), 10)
        self.assertEqual(getattr(instance, "value"), 10)
        await instance.set_value(20)
        self.assertEqual(await instance.get_value(), 20)
        self.assertEqual(getattr(instance, "value"), 20)


class TestClassKosCallable(SpecialsTestCase):

    async def test_class_identity(self):
        """
        Koskari-defined class is a type with KosType metaclass and correct name
        """

        from koskari.lib import KosType

        src_str = """
        (compile/class Foo () 1)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsInstance(result, type)
        self.assertIs(type(result), KosType)
        self.assertEqual(result.__name__, "Foo")
        self.assertEqual(result.__qualname__, "Foo")
        self.assertEqual(result.__module__, "test")


    async def test_class_mro_metaclass_is_kostype(self):
        """
        Koskari-defined class has KosType as metaclass
        """

        from koskari.lib import KosType

        src_str = """
        (compile/class C () 1)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIs(type(result), KosType)


    async def test_call_returns_instance_not_class(self):
        """
        Invoking (Foo) returns an instance, not the class
        """

        src_str = """
        (compile/begin
          (compile/define Foo (compile/class Foo () 1))
          (Foo))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertFalse(isinstance(result, type),
                         "(Foo) should return instance, not class")
        self.assertEqual(result.__class__.__name__, "Foo")


    async def test_define_f_call_returns_instance(self):
        """
        (define f (Foo)) binds f to an instance, not the class
        """

        src_str = """
        (compile/begin
          (compile/define Foo (compile/class Foo () 1))
          (compile/define f (Foo))
          f)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertFalse(isinstance(result, type),
                         "f should be instance, not class")
        self.assertEqual(result.__class__.__name__, "Foo")


    async def test_instance_method_is_bound_and_callable(self):
        """
        Instance method access returns bound method; calling it works
        """

        from koskari.lib import BoundMethod

        src_str = """
        (compile/begin
          (compile/define Foo
            (compile/class Foo ()
              (compile/define greet (compile/lambda [self] "hello"))))
          (compile/define inst (Foo))
          inst)
        """
        expr, mod = self.compile_expr(src_str)
        inst = await expr()
        self.assertIsInstance(inst.greet, BoundMethod)
        self.assertIs(inst.greet.__self__, inst)
        result = await inst.greet()
        self.assertEqual(result, "hello")


    async def test_bound_method_receives_self(self):
        """
        Bound method receives self as first argument
        """

        src_str = """
        (compile/begin
          (compile/define Foo
            (compile/class Foo ()
              (compile/define id_self (compile/lambda [self] self))))
          (compile/define inst (Foo))
          inst)
        """
        expr, mod = self.compile_expr(src_str)
        inst = await expr()
        result = await inst.id_self()
        self.assertIs(result, inst)


class TestClassBytecode(SpecialsTestCase):

    async def test_class_emits_push_pop_collect_build_class(self):
        src_str = """
        (compile/class C () (compile/define x 1))
        """
        code = self.compile_to_code(src_str)
        opcodes = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(self.opcode_named("PUSH_COLLECT"), opcodes)
        self.assertIn(self.opcode_named("POP_COLLECT"), opcodes)
        self.assertIn(self.opcode_named("BUILD_CLASS"), opcodes)
        self.assertIn(self.opcode_named("BUILD_ARGS"), opcodes)


    def opcode_named(self, name):
        from koskari.lib import OpCode

        return getattr(OpCode, "KOS_OP_" + name, None)


# The end.
