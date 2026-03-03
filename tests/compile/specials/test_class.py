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


from . import SpecialsTestCase


class TestClassEmpty(SpecialsTestCase):

    def test_empty_class_binds_and_is_type(self):
        src_str = """
        (compile/begin
          (compile/class C ())
          C)
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertIsInstance(result, type)
        self.assertEqual(result.__name__, "C")


    def test_empty_class_is_kostype(self):
        from koskari.lib import KosType

        src_str = """
        (compile/begin
          (compile/class C ())
          C)
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertIs(type(result), KosType)

        c = result.new()
        self.assertIsInstance(c, result)

class TestClassWithAttribute(SpecialsTestCase):

    def test_class_with_define(self):
        src_str = """
        (compile/begin
          (compile/class C ()
            (compile/define x 42))
          C.x)
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(42, result)


class TestClassWithSuperclass(SpecialsTestCase):

    def test_class_extends_object(self):
        src_str = """
        (compile/begin
          (compile/class C (object)
            (compile/define x 1))
          C.x)
        """
        expr, mod = self.compile_expr(src_str, object=object)
        result = expr().run()
        self.assertEqual(1, result)


class TestClassWithInitAndMethods(SpecialsTestCase):

    def test_class_init_getter_setter_and_direct_attr(self):
        """
        Box has __init__(self, v), get_value(self), set_value(self, new_val).
        Create instance via Box.new(10), verify getter and direct attr, set
        to 20, verify both track. Method calls return awaitables; use run_sync.
        """
        from koskari.lib import run_sync

        src_str = """
        (compile/begin
          (compile/class Box ()
            (compile/define __init__
              (compile/lambda [self v]
                (compile/set-attr self "value" v)))
            (compile/define get_value
              (compile/lambda [self]
                (compile/attr self "value")))
            (compile/define set_value
              (compile/lambda [self new_val]
                (compile/set-attr self "value" new_val))))
          (compile/call (compile/attr Box new) 10))
        """
        expr, mod = self.compile_expr(src_str)
        instance = expr().run()
        self.assertEqual(run_sync(instance.get_value()), 10)
        self.assertEqual(getattr(instance, "value"), 10)
        run_sync(instance.set_value(20))
        self.assertEqual(run_sync(instance.get_value()), 20)
        self.assertEqual(getattr(instance, "value"), 20)


class TestClassBytecode(SpecialsTestCase):

    def test_class_emits_push_pop_class_and_var_bind(self):
        src_str = """
        (compile/class C () (compile/define x 1))
        """
        code = self.compile_to_code(src_str)
        opcodes = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(self.opcode_named("PUSH_CLASS"), opcodes)
        self.assertIn(self.opcode_named("POP_CLASS"), opcodes)
        self.assertIn(self.opcode_named("VAR_BIND"), opcodes)
        self.assertIn(self.opcode_named("BUILD_ARGS"), opcodes)


    def opcode_named(self, name):
        from koskari.lib import OpCode

        return getattr(OpCode, "KOS_OP_" + name, None)


# The end.
