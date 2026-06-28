/**
 * Tree-sitter grammar for Koskari.
 * Parses s-expressions, layout blocks, atoms (symbols, numbers, strings, keywords).
 * Uses seq, choice, repeat, token, field, optional from tree-sitter DSL.
 */

const COMMENT = token(/;[^\n]*/);
const SHEBANG = token(/#![^\n]*/);

// String bodies: any non-quote / non-backslash char, or a backslash escape.
const STR1 = choice(/[^"\\]/, seq('\\', /[\s\S]/));
// Triple-quoted bodies also allow a lone " or "" that does not begin the
// closing delimiter.
const STR3 = choice(/[^"\\]/, seq('\\', /[\s\S]/), /"[^"]/, /""[^"]/);

// Plain / raw (r) / format (f) / byte (b) strings, single or triple quoted.
const STRING = token(choice(
  seq(optional(/[rfbRFB]/), '"""', repeat(STR3), '"""'),
  seq(optional(/[rfbRFB]/), '"', repeat(STR1), '"')
));

const NUMBER = token(prec(1, choice(
  /0[xX][0-9a-fA-F]+/,
  /0[oO][0-7]+/,
  /0[bB][01]+/,
  /-?\d+\/\d+/,
  /-?(?:\d*\.\d+|\d+\.\d*)(?:[eE][+-]?\d+)?[fd]?/,
  /-?\d+[eE][+-]?\d+[fd]?/,
  /-?\d*\.?\d+\+\d*\.?\d*[ij]/,
  /-?(?:\d*\.\d+|\d+\.\d*|\d+)d/,
  /-?\d+/
)));

// Leading-colon keyword (`:foo`). Trailing-colon keywords only occur inside
// delimiters; there they read as a symbol followed by a ':' token, which is
// fine for highlighting, so they need no dedicated rule.
const KEYWORD = token(/:[^\s()\[\]{}"';`,]+/);

// Symbol: a run of non-terminator characters, mirroring the reader's atom
// scan (koskari/lexer/tokens.py). ':' is excluded so that a trailing or
// structural colon splits into its own token — the layout-block terminator
// at depth 0.
const SYMBOL = token(prec(-1, /[^\s()\[\]{}"';`,:]+/));

module.exports = grammar({
  name: 'koskari',

  externals: $ => [$._indent, $._dedent],

  extras: $ => [
    /[\s\f\uFEFF\u2060\u200B]|\r?\n/,
  ],

  // A line-leading symbol is ambiguous between a layout-block header
  // (`name args: <indent>...`) and a plain atom form until the `:` and
  // indent are seen; let the GLR parser explore both.
  conflicts: $ => [
    [$.layout_header, $._atom],
  ],

  rules: {
    source: $ => repeat(choice($.layout_block, $._form, $.comment)),

    layout_block: $ => seq(
      field('head', $.layout_header),
      optional(seq($._indent, repeat(choice($._form, $.comment, $.layout_block)), $._dedent))
    ),

    layout_header: $ => seq(
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
      $._atom
    ),

    // Hidden grouping so string/number/keyword/symbol are direct children of
    // lists and layout headers (queries match them without an `atom` wrapper).
    _atom: $ => choice($.string, $.number, $.keyword, $.symbol),

    comment: $ => choice(COMMENT, SHEBANG),

    string: $ => STRING,

    number: $ => NUMBER,

    keyword: $ => KEYWORD,

    symbol: $ => SYMBOL,

    // Inside delimiters a bare ':' is a separator (e.g. trailing-colon
    // keywords, dict pairs), not a layout terminator.
    _item: $ => choice($._form, $.comment, ':'),

    list: $ => seq('(', repeat($._item), ')'),

    vector: $ => seq('[', repeat($._item), ']'),

    map: $ => seq('{', repeat($._item), '}'),

    collection_tuple: $ => seq('#(', repeat($._item), ')'),

    collection_list: $ => seq('#[', repeat($._item), ']'),

    collection_dict: $ => seq('#{', repeat($._item), '}'),

    quote: $ => seq("'", choice($._form, $.comment)),

    quasiquote: $ => seq('`', choice($._form, $.comment)),

    unquote: $ => seq(',', choice($._form, $.comment)),

    unquote_splicing: $ => seq(',@', choice($._form, $.comment)),
  },
});
