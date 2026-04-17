; Koskari syntax highlighting for Helix

(comment) @comment

(string) @string

(number) @constant.numeric

(keyword) @constant.other.keyword

; Layout block heads and special forms
((_layout_header
  (symbol) @keyword.control))

; List head: treat known specials and layout heads as keyword
; Source: design/SPECIALS.md, design/LAYOUTS.md
((list (symbol) @keyword.control)
 (#any-of? @keyword.control

   "yield-from"
   "yield"
   "with"
   "while"
   "when-main"
   "when"
   "unless"
   "try"
   "syntax-rules"
   "syntax-match"
   "shift"
   "setq-values"
   "setq"
   "set!"
   "return"
   "reset"
   "refq"
   "ref"
   "raise"
   "parameterq"
   "parameterize"
   "or"
   "macro-rules"
   "macro-match"
   "locals"
   "list-each"
   "letrec"
   "let/cc"
   "let-values"
   "let*"
   "let"
   "lambda"
   "import-from"
   "import"
   "if"
   "globals"
   "function-compose"
   "for-each"
   "each"
   "dyn-with"
   "dyn-wind"
   "dict-each"
   "delq"
   "define-values"
   "define-syntax"
   "define-parameter"
   "define-global"
   "define-demanding"
   "define-coroutine"
   "define-class"
   "define"
   "decorate"
   "continue"
   "cond"
   "class"
   "capture/cc"
   "call/cc"
   "break"
   "bindq"
   "bind!"
   "begin"
   "await"
   "attr"
   "and"))

; General symbols
(symbol) @variable.other

; Punctuation and operators
["(" ")" "[" "]" "{" "}"] @punctuation.bracket

["." "," "'" "`"] @keyword.operator

(unquote_splicing
  ["," "@"] @keyword.operator)

(unquote
  [","] @keyword.operator)

; Collection markers
["#(" "#[" "#{"] @punctuation.definition.collection

; The end.