# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

"""
Tests for compile/define-syntax with syntax-rules transformers.
"""


from koskari.compile.syntax import SyntaxRulesTransformer
from koskari.lib import OpCode

from . import SpecialsTestCase


class TestDefineSyntax(SpecialsTestCase):

    def test_define_syntax_unless_with_literal_else(self):
        src_str = """
        (compile/begin
          (define-syntax unless
            (syntax-rules (else:)
              ((unless test body ...)
               (if test None (compile/begin body ...)))
              ((unless test body ... else: alt)
               (if test alt (compile/begin body ...)))))
          (unless True 10)
          (unless False 40 else: 9))
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(40, result)

    def test_hygienic_renaming_for_introduced_symbols(self):
        src_str = """
        (compile/begin
          (compile/define-syntax use-temp
            (syntax-rules ()
              ((use-temp x)
               ((compile/lambda []
                  (define t 99)
                  (add t x))))))
          (define t 2)
          (use-temp 5)
          t)
        """
        expr, mod = self.compile_expr(src_str, add=lambda a, b: a + b)
        result = expr().run()
        self.assertEqual(2, result)

    def test_rule_without_match_raises(self):
        src_str = """
        (compile/begin
          (compile/define-syntax only-one
            (syntax-rules ()
              ((only-one x) x)))
          (only-one 1 2))
        """
        with self.assertRaises(Exception):
            self.compile_expr(src_str)

    def test_define_syntax_uses_tagged_tuple_constant(self):
        code = self.compile_to_code(
            """
            (compile/define-syntax m
              (syntax-rules ()
                ((m x) x)))
            """
        )

        consts = []
        for i in range(code.length()):
            op, arg = code.oparg(i)
            if op == OpCode.KOS_OP_CONST:
                consts.append(code.constant(arg))

        self.assertFalse(any(isinstance(v, SyntaxRulesTransformer) for v in consts))
        self.assertTrue(any(isinstance(v, tuple) for v in consts))


# The end.
