/**
 * Tree-sitter grammar for Koskari.
 * Parses s-expressions, layout blocks, atoms (symbols, numbers, strings, keywords).
 * Uses seq, choice, repeat, token, field, optional from tree-sitter DSL.
 */

const COMMENT = token(/;[^\n]*/);
const SHEBANG = token(/^#![^\n]*/);

const STRING = token(choice(
  seq('"', repeat(choice(/[^"\\]/, seq('\\', /./))), '"'),
  seq('r"', repeat(/[^"]/), '"'),
  seq('f"', repeat(choice(/[^"\\{}]/, seq('\\', /./), /{{|}}/, /\{[^{}]*\}/)), '"'),
  seq('"""', repeat(choice(/[^"\\]/, seq('\\', /./), /"(?!"")/)), '"""'),
  seq('r"""', repeat(/[^"]/), '"""'),
  seq('f"""', repeat(choice(/[^"\\{}]/, seq('\\', /./), /{{|}}/, /\{[^{}]*\}/)), '"""')
));

const NUMBER = token(choice(
  /0[xX][0-9a-fA-F]+/,
  /0[oO][0-7]+/,
  /0[bB][01]+/,
  /-?\d+\/\d+/,
  /-?(?:\d*\.\d+|\d+\.\d*)(?:[eE][+-]?\d+)?[fd]?/,
  /-?\d+[eE][+-]?\d+[fd]?/,
  /-?\d+(?:\.\d+)?[+-]\d*\.?\d*[ij]/,
  /-?\d+(?:\.\d*|\d*)?\.?d\b/,
  /-?\d+/
));

const KEYWORD = token(choice(
  /:(?:[^\s()\[\]{};"'`\\,]|\\[^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})+/,
  /(?:[^\s()\[\]{};"'`\\,:]|\\[^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})+:/
));

const SYMBOL = token(/[a-zA-Z_][a-zA-Z0-9_?!*+<=>-]*|[!$%&*+\-./:<=>?@^_~]/);

module.exports = grammar({
  name: 'koskari',

  externals: $ => [$._indent, $._dedent],

  extras: $ => [
    /[\s\f\uFEFF\u2060\u200B]|\r?\n/,
  ],

  rules: {
    source: $ => repeat(choice($._layout_block, $._form, $.comment)),

    _layout_block: $ => seq(
      field('head', $._layout_header),
      optional(seq($._indent, repeat(choice($._form, $.comment, $._layout_block)), $._dedent))
    ),

    _layout_header: $ => seq(
      field('name', $.symbol),
      repeat(choice($._form, $.comment)),
      ':'
    ),

    _form: $ => choice(
      $.list,
      $.vector,
      $.map,
      $.collection_tuple,
      $.collection_list,
      $.collection_dict,
      $.quote,
      $.quasiquote,
      $.unquote,
      $.unquote_splicing,
      $.atom
    ),

    atom: $ => choice($.string, $.number, $.keyword, $.symbol),

    comment: $ => choice(COMMENT, SHEBANG),

    string: $ => STRING,

    number: $ => NUMBER,

    keyword: $ => KEYWORD,

    symbol: $ => SYMBOL,

    list: $ => seq('(', repeat(choice($._form, $.comment)), ')'),

    vector: $ => seq('[', repeat(choice($._form, $.comment)), ']'),

    map: $ => seq('{', repeat(choice($._form, $.comment)), '}'),

    collection_tuple: $ => seq('#(', repeat(choice($._form, $.comment)), ')'),

    collection_list: $ => seq('#[', repeat(choice($._form, $.comment)), ']'),

    collection_dict: $ => seq('#{', repeat(choice($._form, $.comment)), '}'),

    quote: $ => seq("'", choice($._form, $.comment)),

    quasiquote: $ => seq('`', choice($._form, $.comment)),

    unquote: $ => seq(',', choice($._form, $.comment)),

    unquote_splicing: $ => seq(',@', choice($._form, $.comment)),
  },
});
