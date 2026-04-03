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

   "with"
   "while"
   "when-main"
   "when"
   "unless"
   "try"
   "shift"
   "setq"
   "set!"
   "reset"
   "refq"
   "ref"
   "raise"
   "parameterq"
   "parameterize"
   "or"
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
   "for-each"
   "each"
   "dyn-with"
   "dyn-wind"
   "dict-each"
   "delq"
   "define-syntax"
   "define-parameter"
   "define-coroutine"
   "define"
   "continue"
   "cond"
   "class"
   "capture/cc"
   "call/cc"
   "break"
   "bindq"
   "bind!"
   "begin"
   "attr"
   "and"
   "None?"))

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